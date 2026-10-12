//===- SyncTest.cpp - VkFence / vkQueueSubmit tests ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "Sync.h"
#include "Buffer.h"
#include "CommandBuffer.h"
#include "EntryPoints.h"
#include "ExecutionLifetime.h"
#include "Icd.h"
#include "Image.h"
#include "Objects.h"
#include "Pipeline.h"

#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Target/SPIRV/Serialization.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Testing/Support/Error.h"

#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

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

// Roadmap L398: a direct, deterministic (non-timing-dependent) test of
// `Fence`'s deferred-destroy bookkeeping, exercising the exact sequence
// that used to free a `Fence` out from under a `QueueExecutor` worker
// thread still about to call `signal()` on it -- a live CTS sweep's
// `gdb`-inspected hang found that crash as a `pthread_mutex_lock` stuck
// forever on already-freed memory (see `Fence::requestDestroy`'s comment,
// `Sync.h`). Simulates `vkQueueSubmit`'s retain, `vkDestroyFence` arriving
// *before* the fence-signal task actually runs (the exact ordering a
// real crash needs), and only then the task itself releasing/signaling --
// entirely through `Fence`'s own public API, so this is reliable under
// any thread scheduling, unlike a true multi-thread stress test.
TEST(FenceLifetimeTest, DestroyDefersUntilPendingSignalTaskReleases) {
  auto *F = new Fence(/*Signaled=*/false);

  // `vkQueueSubmit`'s retain, before enqueuing the fence-signal task.
  F->retainForPendingSignal();

  // `vkDestroyFence` arrives first (the hazardous ordering): must defer,
  // not free `F` here.
  EXPECT_FALSE(F->requestDestroy(nullptr));

  // The fence-signal task finally runs: signaling a still-live `Fence` is
  // well defined (no use-after-free)...
  F->signal();
  EXPECT_TRUE(F->isSignaled());

  // ...and releasing it now reports the deferred destroy is due, so the
  // caller (the real `makeFenceSignalTask` closure) deletes `F` itself.
  const VkAllocationCallbacks *Callbacks =
      reinterpret_cast<const VkAllocationCallbacks *>(
          static_cast<uintptr_t>(1));
  EXPECT_TRUE(F->releasePendingSignalAndTakeDeferredDestroy(Callbacks));
  EXPECT_EQ(Callbacks, nullptr); // The `nullptr` given to `requestDestroy`.

  delete F;
}

// The ordinary case -- no `QueueExecutor` task ever enqueued for this
// fence at all -- must still let `vkDestroyFence` free it immediately,
// matching every other `TEST_F(SyncTest, ...)` in this file that destroys
// a fence without ever calling `vkQueueSubmit` on it (`ResetFenceReturnsTo
// Unsignaled` above).
TEST(FenceLifetimeTest, DestroyWithNoPendingSignalFreesImmediately) {
  auto *F = new Fence(/*Signaled=*/false);
  EXPECT_TRUE(F->requestDestroy(nullptr));
  delete F;
}

// Roadmap L409: `PendingExecutionRefCounted` is `Fence`'s own
// `L398`-era retain/release/deferred-destroy pattern (immediately above)
// generalized to `Pipeline`/`RenderPass`/`Framebuffer` -- applied to
// `Pipeline` here (the type the standalone `leakprobe_uaf.cpp` reproducer
// proved vulnerable), tested directly through the mixin's own API,
// deterministically, for the same reason the `FenceLifetimeTest`s above
// do: no real thread scheduling needed to exercise every ordering.
TEST(ExecutionLifetimeTest, DestroyDefersUntilRetainedTaskReleases) {
  auto *P = new PendingExecutionRefCounted();

  // `vkQueueSubmit`'s retain-sweep, before enqueuing the submission task.
  P->retainForExecution();

  // `vkDestroyPipeline` arrives first (the hazardous ordering): must
  // defer, not free `P` here.
  EXPECT_FALSE(P->requestDestroy(nullptr));

  // The submission task finishes running (whether it actually executed
  // the command buffer, failed, or skipped because the device was
  // already lost -- all three call `releaseAndTakeDeferredDestroy`
  // exactly once, same as here): releasing now reports the deferred
  // destroy is due.
  const VkAllocationCallbacks *Callbacks =
      reinterpret_cast<const VkAllocationCallbacks *>(
          static_cast<uintptr_t>(1));
  EXPECT_TRUE(P->releaseAndTakeDeferredDestroy(Callbacks));
  EXPECT_EQ(Callbacks, nullptr); // The `nullptr` given to `requestDestroy`.

  delete P;
}

// The ordinary case -- no `QueueExecutor` task ever enqueued for this
// object at all (e.g. a pipeline created and destroyed without ever being
// bound in a submitted command buffer) -- must still free it immediately.
TEST(ExecutionLifetimeTest, DestroyWithNoPendingTasksFreesImmediately) {
  auto *P = new PendingExecutionRefCounted();
  EXPECT_TRUE(P->requestDestroy(nullptr));
  delete P;
}

// Multiple overlapping tasks (e.g. a pipeline bound in two submissions
// still in flight at once): the object must not be freed until *every*
// one of them has released it, not just the first.
TEST(ExecutionLifetimeTest, DestroyDefersUntilAllRetainedTasksRelease) {
  auto *P = new PendingExecutionRefCounted();
  P->retainForExecution();
  P->retainForExecution();
  EXPECT_FALSE(P->requestDestroy(nullptr));

  const VkAllocationCallbacks *Callbacks = nullptr;
  // The first of the two tasks to finish must not yet see a deferred
  // destroy: the second task might still be reading `P`.
  EXPECT_FALSE(P->releaseAndTakeDeferredDestroy(Callbacks));
  // Only once the second (last) task releases does the deferred destroy
  // become due.
  EXPECT_TRUE(P->releaseAndTakeDeferredDestroy(Callbacks));

  delete P;
}

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
TEST_F(SyncTest,
       TimelineSemaphoreCrossQueueSubmitChainCompletesAfterHostSignal) {
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
TEST_F(SyncTest,
       QueueSubmit2EnqueuesPromptlyThenLatchesDeviceLostOnUnmetBinaryWait) {
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

// Roadmap L236(a): `bindInfoCount == 0` is explicitly spec-legal
// regardless of `sparseBinding`'s state (this ICD's own feature bit stays
// `VK_FALSE`), and the fence must still be signaled -- the same "empty
// batch of work" contract `vkQueueSubmit`/`vkQueueSubmit2` already honor
// with `submitCount == 0`.
TEST_F(SyncTest, QueueBindSparseZeroBindInfoCountSignalsFence) {
  VkFenceCreateInfo FenceInfo{};
  VkFence Fence = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFence(Device, &FenceInfo, nullptr, &Fence), VK_SUCCESS);
  EXPECT_EQ(vkGetFenceStatus(Device, Fence), VK_NOT_READY);

  EXPECT_EQ(vkQueueBindSparse(Queue, 0, nullptr, Fence), VK_SUCCESS);
  EXPECT_EQ(vkWaitForFences(Device, 1, &Fence, VK_TRUE, UINT64_MAX),
            VK_SUCCESS);
  EXPECT_EQ(vkGetFenceStatus(Device, Fence), VK_SUCCESS);

  vkDestroyFence(Device, Fence, nullptr);
}

// Mirrors `QueueSubmit2BinarySemaphoreSignalThenWaitSucceeds` above, but
// through `vkQueueBindSparse`'s own (empty-bind-array) wait/signal
// semaphores -- this ICD threads a `VkBindSparseInfo`'s synchronization
// through the identical `QueueExecutor`/`makeSubmissionTask` machinery
// (Sync.cpp), so ordinary semaphore signal-then-wait must still work
// exactly the same even though no actual memory bind ever occurs.
TEST_F(SyncTest, QueueBindSparseBinarySemaphoreSignalThenWaitSucceeds) {
  VkSemaphoreCreateInfo SemInfo{};
  VkSemaphore Sem = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSemaphore(Device, &SemInfo, nullptr, &Sem), VK_SUCCESS);

  VkBindSparseInfo Signal{};
  Signal.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
  Signal.signalSemaphoreCount = 1;
  Signal.pSignalSemaphores = &Sem;
  ASSERT_EQ(vkQueueBindSparse(Queue, 1, &Signal, VK_NULL_HANDLE), VK_SUCCESS);

  VkBindSparseInfo Wait{};
  Wait.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
  Wait.waitSemaphoreCount = 1;
  Wait.pWaitSemaphores = &Sem;
  EXPECT_EQ(vkQueueBindSparse(Queue, 1, &Wait, VK_NULL_HANDLE), VK_SUCCESS);
  EXPECT_EQ(vkQueueWaitIdle(Queue), VK_SUCCESS);

  vkDestroySemaphore(Device, Sem, nullptr);
}

// (Roadmap H46) `vkQueueBindSparse`'s `pBufferBinds` is now processed for
// real (see that function's own comment, Sync.cpp) rather than ignored --
// confirms a `VkSparseMemoryBind` reaches the target `Buffer` via
// `bindSparseRange`. No real application can construct this (`buffer` was
// never created with `VK_BUFFER_CREATE_SPARSE_BINDING_BIT`, which
// `isValidBufferCreateInfo` already rejects), so this exercises the ICD's
// internal plumbing directly rather than a spec-legal CTS-reachable path.
TEST_F(SyncTest, QueueBindSparseBufferBindAppliesSparseRange) {
  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = 128;
  BufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  VkBuffer Buf = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Buf), VK_SUCCESS);

  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = 128;
  AllocInfo.memoryTypeIndex = 0;
  VkDeviceMemory Memory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Memory),
            VK_SUCCESS);

  VkSparseMemoryBind SMB{};
  SMB.resourceOffset = 0;
  SMB.size = 128;
  SMB.memory = Memory;
  SMB.memoryOffset = 0;

  VkSparseBufferMemoryBindInfo BufBindInfo{};
  BufBindInfo.buffer = Buf;
  BufBindInfo.bindCount = 1;
  BufBindInfo.pBinds = &SMB;

  VkBindSparseInfo BindInfo{};
  BindInfo.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
  BindInfo.bufferBindCount = 1;
  BindInfo.pBufferBinds = &BufBindInfo;
  ASSERT_EQ(vkQueueBindSparse(Queue, 1, &BindInfo, VK_NULL_HANDLE),
            VK_SUCCESS);
  EXPECT_EQ(vkQueueWaitIdle(Queue), VK_SUCCESS);

  ASSERT_EQ(fromHandle<Buffer>(Buf)->ranges().size(), 1u);
  EXPECT_EQ(fromHandle<Buffer>(Buf)->ranges()[0].Memory,
            fromHandle<DeviceMemory>(Memory));
  EXPECT_EQ(fromHandle<Buffer>(Buf)->ranges()[0].ResourceOffset, 0u);
  EXPECT_EQ(fromHandle<Buffer>(Buf)->ranges()[0].Size, 128u);

  vkDestroyBuffer(Device, Buf, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
}

// Mirrors the buffer-bind test above for `pImageOpaqueBinds`, confirming
// a `VkSparseMemoryBind` reaches the target `Image` via the same
// `bindSparseRange` method.
TEST_F(SyncTest, QueueBindSparseImageOpaqueBindAppliesSparseRange) {
  VkImageCreateInfo ImageInfo{};
  ImageInfo.imageType = VK_IMAGE_TYPE_2D;
  ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ImageInfo.extent = {4, 4, 1};
  ImageInfo.mipLevels = 1;
  ImageInfo.arrayLayers = 1;
  ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  ImageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
  VkImage Img = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &Img), VK_SUCCESS);

  VkMemoryRequirements Reqs{};
  vkGetImageMemoryRequirements(Device, Img, &Reqs);
  VkMemoryAllocateInfo AllocInfo2{};
  AllocInfo2.allocationSize = Reqs.size;
  AllocInfo2.memoryTypeIndex = 0;
  VkDeviceMemory Memory2 = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo2, nullptr, &Memory2),
            VK_SUCCESS);

  VkSparseMemoryBind SMB2{};
  SMB2.resourceOffset = 0;
  SMB2.size = Reqs.size;
  SMB2.memory = Memory2;
  SMB2.memoryOffset = 0;

  VkSparseImageOpaqueMemoryBindInfo ImgBindInfo{};
  ImgBindInfo.image = Img;
  ImgBindInfo.bindCount = 1;
  ImgBindInfo.pBinds = &SMB2;

  VkBindSparseInfo BindInfo2{};
  BindInfo2.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
  BindInfo2.imageOpaqueBindCount = 1;
  BindInfo2.pImageOpaqueBinds = &ImgBindInfo;
  ASSERT_EQ(vkQueueBindSparse(Queue, 1, &BindInfo2, VK_NULL_HANDLE),
            VK_SUCCESS);
  EXPECT_EQ(vkQueueWaitIdle(Queue), VK_SUCCESS);

  ASSERT_EQ(fromHandle<Image>(Img)->ranges().size(), 1u);
  EXPECT_EQ(fromHandle<Image>(Img)->ranges()[0].Memory,
            fromHandle<DeviceMemory>(Memory2));
  EXPECT_EQ(fromHandle<Image>(Img)->ranges()[0].Size, Reqs.size);

  vkDestroyImage(Device, Img, nullptr);
  vkFreeMemory(Device, Memory2, nullptr);
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

// (Roadmap L358) The cross-thread shape this fix addresses, mirroring
// `QueryPoolTest.WaitAvailableBlocksUntilAnotherThreadMarksItAvailable`'s
// (`L354`) own precedent: a `vkCmdSetEvent` recorded onto one thread
// (standing in for a `QueueExecutor` worker thread) and an `isSignaled`
// poll from another (standing in for a host thread's `vkGetEventStatus`)
// must observe the same state once the setter has actually run. Before
// this fix, `Event::Signaled` was a plain, unsynchronized `bool`, so there
// was no guarantee the setting thread's write was ever visible to the
// polling thread at all (undefined behavior, not merely a timing race).
TEST(EventTest, SetFromAnotherThreadIsObservedByIsSignaled) {
  Event Ev(/*Signaled=*/false);
  EXPECT_FALSE(Ev.isSignaled());

  constexpr auto SetDelay = std::chrono::milliseconds(200);
  std::thread Setter([&] {
    std::this_thread::sleep_for(SetDelay);
    Ev.set();
  });

  // Poll (rather than block, since `Event` has no condition variable --
  // `vkCmdWaitEvents` itself never spins, see `runWaitEvents`) until
  // either the setter thread's write becomes visible or a generous
  // timeout elapses; a pre-fix build could in principle still observe the
  // write promptly on most real hardware (a data race is undefined
  // behavior, not a guaranteed hang), so this test's real value is
  // documenting/pinning the now-correct synchronized access, with
  // ThreadSanitizer as the actual race detector.
  auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!Ev.isSignaled() && std::chrono::steady_clock::now() < Deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  EXPECT_TRUE(Ev.isSignaled());

  Setter.join();
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

// (Roadmap L252) `parseSafetyNetTimeoutMsEnv` is exercised directly
// (rather than through `getSafetyNetTimeoutNs()`, whose value is cached
// for the lifetime of the process the first time anything in this test
// binary blocks on a fence/semaphore -- unavoidable given every other
// test above does exactly that) so every input case is still covered
// without depending on test order or a real environment variable.
TEST(SafetyNetTimeoutEnvTest, UnsetFallsBackToDefault) {
  EXPECT_EQ(parseSafetyNetTimeoutMsEnv(nullptr), DefaultSafetyNetTimeoutNs);
}

TEST(SafetyNetTimeoutEnvTest, EmptyFallsBackToDefault) {
  EXPECT_EQ(parseSafetyNetTimeoutMsEnv(""), DefaultSafetyNetTimeoutNs);
}

TEST(SafetyNetTimeoutEnvTest, NonNumericFallsBackToDefault) {
  EXPECT_EQ(parseSafetyNetTimeoutMsEnv("not-a-number"),
            DefaultSafetyNetTimeoutNs);
}

TEST(SafetyNetTimeoutEnvTest, ZeroFallsBackToDefault) {
  EXPECT_EQ(parseSafetyNetTimeoutMsEnv("0"), DefaultSafetyNetTimeoutNs);
}

TEST(SafetyNetTimeoutEnvTest, NegativeFallsBackToDefault) {
  EXPECT_EQ(parseSafetyNetTimeoutMsEnv("-5"), DefaultSafetyNetTimeoutNs);
}

TEST(SafetyNetTimeoutEnvTest, ValidOverrideIsHonoredAndConvertedToNs) {
  EXPECT_EQ(parseSafetyNetTimeoutMsEnv("30000"), 30'000'000'000ULL);
}

} // namespace

// Roadmap L398: spec-valid repro attempt for the `QueueExecutor`/`Fence`
// teardown hang. Many host threads concurrently create a fence, submit a
// dispatch with it, wait on just that one fence (never a device-wide
// `vkDeviceWaitIdle`, matching the ordinary CTS idiom the handoff called
// out), then destroy it -- while *other* threads keep doing the same on
// the same two queues at the same time, so a destroyed fence's
// `QueueExecutor` task might still be mid-flight when freed, without a
// device-wide wait ever masking the race. Each thread gets its own
// command buffer (so no two threads ever resubmit the same one without
// `VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT`) and queue access to each
// of the two shared queues is serialized by a mutex (the spec's "host
// access to `queue` must be externally synchronized" requirement) --
// everything else is left as racy as real concurrent CTS usage can be.
TEST_F(SyncTest, StressFenceCreateSubmitWaitDestroyNoGlobalWait) {
  constexpr int kThreads = 8;
  constexpr int kItersPerThread = 400;

  std::vector<VkCommandBuffer> CmdBufs(kThreads);
  for (int T = 0; T != kThreads; ++T) {
    VkCommandBufferAllocateInfo AllocInfo{};
    AllocInfo.commandPool = (T % 2 == 0) ? Pool : PoolB;
    AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    AllocInfo.commandBufferCount = 1;
    ASSERT_EQ(vkAllocateCommandBuffers(Device, &AllocInfo, &CmdBufs[T]),
              VK_SUCCESS);
    VkCommandBufferBeginInfo BeginInfo{};
    ASSERT_EQ(vkBeginCommandBuffer(CmdBufs[T], &BeginInfo), VK_SUCCESS);
    vkCmdBindPipeline(CmdBufs[T], VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
    vkCmdDispatch(CmdBufs[T], 2, 2, 2);
    ASSERT_EQ(vkEndCommandBuffer(CmdBufs[T]), VK_SUCCESS);
  }

  std::mutex QueueMutex, QueueBMutex;
  std::atomic<bool> Failed{false};
  auto Worker = [&](VkQueue Q, std::mutex &QMutex, VkCommandBuffer CB) {
    for (int I = 0; I != kItersPerThread && !Failed.load(); ++I) {
      VkFenceCreateInfo FenceInfo{};
      VkFence F = VK_NULL_HANDLE;
      if (vkCreateFence(Device, &FenceInfo, nullptr, &F) != VK_SUCCESS) {
        Failed = true;
        return;
      }
      VkSubmitInfo Submit{};
      Submit.commandBufferCount = 1;
      Submit.pCommandBuffers = &CB;
      VkResult SubmitResult;
      {
        std::lock_guard<std::mutex> Lock(QMutex);
        SubmitResult = vkQueueSubmit(Q, 1, &Submit, F);
      }
      if (SubmitResult != VK_SUCCESS) {
        Failed = true;
        return;
      }
      if (vkWaitForFences(Device, 1, &F, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        Failed = true;
        return;
      }
      vkDestroyFence(Device, F, nullptr);
    }
  };
  std::vector<std::thread> Threads;
  for (int T = 0; T != kThreads; ++T)
    Threads.emplace_back(Worker, (T % 2 == 0) ? Queue : QueueB,
                         std::ref((T % 2 == 0) ? QueueMutex : QueueBMutex),
                         CmdBufs[T]);
  for (auto &Th : Threads)
    Th.join();
  EXPECT_FALSE(Failed.load());
}

// Roadmap L409: the real, end-to-end scenario `leakprobe_uaf.cpp` first
// proved crashes without this fix -- submit a dispatch bound to a
// dedicated pipeline, then *immediately* destroy that pipeline, with no
// wait at all in between (spec-legal: the application only has to keep
// the command buffer alive until the submission completes, not the
// resources recorded into it). Before this fix, the `QueueExecutor`
// worker thread's own still-pending `executeCommandBuffer` call read the
// `Pipeline` through `CommandBuffer.cpp`'s raw `Cmd.Pipeline` right after
// this test's `vkDestroyPipeline` had already freed it -- a real,
// nondeterministically-timed use-after-free that this test cannot force
// to manifest on every run, but which this test's pipeline is retained
// across (`retainForExecution`/`requestDestroy`) deterministically
// regardless of scheduling, so this is a direct regression test even
// though the crash itself was timing-dependent.
TEST_F(SyncTest, SubmitThenImmediatelyDestroyPipelineWithNoWaitSucceeds) {
  VkPipelineLayoutCreateInfo LayoutInfo{};
  VkPipelineLayout TestLayout = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &TestLayout),
            VK_SUCCESS);

  VkComputePipelineCreateInfo PipelineInfo{};
  PipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  PipelineInfo.stage.module = Module;
  PipelineInfo.stage.pName = "main";
  PipelineInfo.layout = TestLayout;
  VkPipeline TestPipeline = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &PipelineInfo,
                                     nullptr, &TestPipeline),
            VK_SUCCESS);

  VkCommandBufferAllocateInfo AllocInfo{};
  AllocInfo.commandPool = Pool;
  AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  AllocInfo.commandBufferCount = 1;
  VkCommandBuffer TestCmdBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &AllocInfo, &TestCmdBuf),
            VK_SUCCESS);
  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(TestCmdBuf, &BeginInfo), VK_SUCCESS);
  vkCmdBindPipeline(TestCmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, TestPipeline);
  vkCmdDispatch(TestCmdBuf, 2, 2, 2);
  ASSERT_EQ(vkEndCommandBuffer(TestCmdBuf), VK_SUCCESS);

  VkSubmitInfo Submit{};
  Submit.commandBufferCount = 1;
  Submit.pCommandBuffers = &TestCmdBuf;
  ASSERT_EQ(vkQueueSubmit(Queue, 1, &Submit, VK_NULL_HANDLE), VK_SUCCESS);

  // The hazardous ordering: destroy everything the just-enqueued
  // submission references with no wait at all. Pre-fix, this raced the
  // `QueueExecutor` worker thread's own read of `TestPipeline`.
  vkDestroyPipeline(Device, TestPipeline, nullptr);
  vkDestroyPipelineLayout(Device, TestLayout, nullptr);

  // Only now wait for the submission to actually finish -- by this point,
  // pre-fix, the worker thread may already have run past (or crashed on)
  // its read of the freed `TestPipeline`.
  EXPECT_EQ(vkQueueWaitIdle(Queue), VK_SUCCESS);

  vkFreeCommandBuffers(Device, Pool, 1, &TestCmdBuf);
}

// (Roadmap L410) `Buffer`'s analog of
// `SubmitThenImmediatelyDestroyPipelineWithNoWaitSucceeds` above: the same
// hazardous ordering (destroy with no wait, right after `vkQueueSubmit`
// returns), but for a `Buffer` referenced via `vkCmdCopyBuffer`'s
// `SrcBuffer`/`DstBuffer` fields instead of `Cmd.Pipeline`.
TEST_F(SyncTest, SubmitThenImmediatelyDestroyBufferWithNoWaitSucceeds) {
  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = 16;
  BufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  VkBuffer SrcBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &SrcBuf), VK_SUCCESS);
  BufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VkBuffer DstBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &DstBuf), VK_SUCCESS);

  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = 16;
  AllocInfo.memoryTypeIndex = 0;
  VkDeviceMemory SrcMemory = VK_NULL_HANDLE, DstMemory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &SrcMemory),
            VK_SUCCESS);
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &DstMemory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindBufferMemory(Device, SrcBuf, SrcMemory, 0), VK_SUCCESS);
  ASSERT_EQ(vkBindBufferMemory(Device, DstBuf, DstMemory, 0), VK_SUCCESS);

  VkCommandBufferAllocateInfo CmdAllocInfo{};
  CmdAllocInfo.commandPool = Pool;
  CmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  CmdAllocInfo.commandBufferCount = 1;
  VkCommandBuffer TestCmdBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &CmdAllocInfo, &TestCmdBuf),
            VK_SUCCESS);
  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(TestCmdBuf, &BeginInfo), VK_SUCCESS);
  VkBufferCopy Region{};
  Region.size = 16;
  vkCmdCopyBuffer(TestCmdBuf, SrcBuf, DstBuf, 1, &Region);
  ASSERT_EQ(vkEndCommandBuffer(TestCmdBuf), VK_SUCCESS);

  VkSubmitInfo Submit{};
  Submit.commandBufferCount = 1;
  Submit.pCommandBuffers = &TestCmdBuf;
  ASSERT_EQ(vkQueueSubmit(Queue, 1, &Submit, VK_NULL_HANDLE), VK_SUCCESS);

  // The hazardous ordering: destroy the `VkBuffer` wrapper objects the
  // just-enqueued submission references with no wait at all. Pre-fix,
  // this raced the `QueueExecutor` worker thread's own read of
  // `SrcBuf`/`DstBuf` (the C++ `Buffer` objects themselves -- their
  // `size()`/`data()`, not the contents of the backing `VkDeviceMemory`,
  // which is a separate Vulkan object; freeing memory still bound to a
  // buffer referenced by a submission not yet complete is independently
  // invalid API usage per the spec, so this test deliberately leaves
  // `SrcMemory`/`DstMemory` alive until after the wait below, matching
  // `SubmitThenImmediatelyDestroyPipelineWithNoWaitSucceeds`'s scope: the
  // UAF class `L409`/`L410` fix is about the wrapper object's lifetime,
  // not shared/backing memory contents).
  vkDestroyBuffer(Device, SrcBuf, nullptr);
  vkDestroyBuffer(Device, DstBuf, nullptr);

  // Only now wait for the submission to actually finish -- by this point,
  // pre-fix, the worker thread may already have run past (or crashed on)
  // its read of the freed `Buffer` objects.
  EXPECT_EQ(vkQueueWaitIdle(Queue), VK_SUCCESS);

  vkFreeMemory(Device, SrcMemory, nullptr);
  vkFreeMemory(Device, DstMemory, nullptr);
  vkFreeCommandBuffers(Device, Pool, 1, &TestCmdBuf);
}

// (Roadmap L410) `Image`/`ImageView`'s analog of
// `SubmitThenImmediatelyDestroyBufferWithNoWaitSucceeds` above: the same
// hazardous ordering, but for `Image`s referenced via `vkCmdCopyImage`'s
// `SrcImage`/`DstImage` fields.
TEST_F(SyncTest, SubmitThenImmediatelyDestroyImageWithNoWaitSucceeds) {
  VkImageCreateInfo ImageInfo{};
  ImageInfo.imageType = VK_IMAGE_TYPE_2D;
  ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ImageInfo.extent = {2, 2, 1};
  ImageInfo.mipLevels = 1;
  ImageInfo.arrayLayers = 1;
  ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  ImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  VkImage SrcImg = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &SrcImg), VK_SUCCESS);
  ImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  VkImage DstImg = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &DstImg), VK_SUCCESS);

  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = 64;
  AllocInfo.memoryTypeIndex = 0;
  VkDeviceMemory SrcMemory = VK_NULL_HANDLE, DstMemory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &SrcMemory),
            VK_SUCCESS);
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &DstMemory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindImageMemory(Device, SrcImg, SrcMemory, 0), VK_SUCCESS);
  ASSERT_EQ(vkBindImageMemory(Device, DstImg, DstMemory, 0), VK_SUCCESS);

  VkCommandBufferAllocateInfo CmdAllocInfo{};
  CmdAllocInfo.commandPool = Pool;
  CmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  CmdAllocInfo.commandBufferCount = 1;
  VkCommandBuffer TestCmdBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &CmdAllocInfo, &TestCmdBuf),
            VK_SUCCESS);
  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(TestCmdBuf, &BeginInfo), VK_SUCCESS);
  VkImageCopy Region{};
  Region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  Region.extent = {2, 2, 1};
  vkCmdCopyImage(TestCmdBuf, SrcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                DstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);
  ASSERT_EQ(vkEndCommandBuffer(TestCmdBuf), VK_SUCCESS);

  VkSubmitInfo Submit{};
  Submit.commandBufferCount = 1;
  Submit.pCommandBuffers = &TestCmdBuf;
  ASSERT_EQ(vkQueueSubmit(Queue, 1, &Submit, VK_NULL_HANDLE), VK_SUCCESS);

  // The hazardous ordering: destroy the `VkImage` wrapper objects the
  // just-enqueued submission references with no wait at all (see
  // `SubmitThenImmediatelyDestroyBufferWithNoWaitSucceeds`'s comment for
  // why backing `VkDeviceMemory` is freed only after the wait below --
  // the same reasoning applies here).
  vkDestroyImage(Device, SrcImg, nullptr);
  vkDestroyImage(Device, DstImg, nullptr);

  // Only now wait for the submission to actually finish -- by this point,
  // pre-fix, the worker thread may already have run past (or crashed on)
  // its read of the freed `Image` objects.
  EXPECT_EQ(vkQueueWaitIdle(Queue), VK_SUCCESS);

  vkFreeMemory(Device, SrcMemory, nullptr);
  vkFreeMemory(Device, DstMemory, nullptr);
  vkFreeCommandBuffers(Device, Pool, 1, &TestCmdBuf);
}

// (Roadmap L410) `DescriptorSet`'s analog of
// `SubmitThenImmediatelyDestroyBufferWithNoWaitSucceeds`/
// `...ImageWithNoWaitSucceeds` above, and the one that actually closes the
// "descriptor-indirect" gap those two left open: a `vkCmdBindDescriptorSets`
// command only ever captures a raw `DescriptorSet *` in
// `RecordedCommand::DescriptorSets`, with no further reference to the
// `BufferView`/`Sampler` objects the set itself has bound -- those are only
// reachable by walking the set's own storage (`collectDescriptorSetRefs`,
// `Sync.cpp`). This test binds one of each new mixin type (`DescriptorSet`
// itself, a `BufferView` behind a storage-texel-buffer binding, and a
// `Sampler` behind a combined-image-sampler binding) and then frees/destroys
// all three, plus the descriptor pool itself via `vkResetDescriptorPool`,
// before waiting for the submission to complete -- exercising
// `DescriptorPool::free`'s per-set deferred-destroy guard as well as the new
// mixins' own.
TEST_F(SyncTest, SubmitThenImmediatelyDestroyDescriptorSetWithNoWaitSucceeds) {
  VkDescriptorSetLayoutBinding Bindings[2]{};
  Bindings[0].binding = 0;
  Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
  Bindings[0].descriptorCount = 1;
  Bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  Bindings[1].binding = 1;
  Bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  Bindings[1].descriptorCount = 1;
  Bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
  SetLayoutInfo.bindingCount = 2;
  SetLayoutInfo.pBindings = Bindings;
  VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreateDescriptorSetLayout(Device, &SetLayoutInfo, nullptr, &SetLayout),
      VK_SUCCESS);

  VkPipelineLayoutCreateInfo PLayoutInfo{};
  PLayoutInfo.setLayoutCount = 1;
  PLayoutInfo.pSetLayouts = &SetLayout;
  VkPipelineLayout PLayout = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineLayout(Device, &PLayoutInfo, nullptr, &PLayout),
            VK_SUCCESS);

  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = 16;
  BufferInfo.usage = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
  VkBuffer Buf = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Buf), VK_SUCCESS);
  VkMemoryAllocateInfo BufAllocInfo{};
  BufAllocInfo.allocationSize = 16;
  BufAllocInfo.memoryTypeIndex = 0;
  VkDeviceMemory BufMemory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &BufAllocInfo, nullptr, &BufMemory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindBufferMemory(Device, Buf, BufMemory, 0), VK_SUCCESS);
  VkBufferViewCreateInfo ViewInfo{};
  ViewInfo.buffer = Buf;
  ViewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
  ViewInfo.range = VK_WHOLE_SIZE;
  VkBufferView View = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBufferView(Device, &ViewInfo, nullptr, &View), VK_SUCCESS);

  VkImageCreateInfo ImageInfo{};
  ImageInfo.imageType = VK_IMAGE_TYPE_2D;
  ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ImageInfo.extent = {2, 2, 1};
  ImageInfo.mipLevels = 1;
  ImageInfo.arrayLayers = 1;
  ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  ImageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
  VkImage Img = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &Img), VK_SUCCESS);
  VkMemoryAllocateInfo ImgAllocInfo{};
  ImgAllocInfo.allocationSize = 64;
  ImgAllocInfo.memoryTypeIndex = 0;
  VkDeviceMemory ImgMemory = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateMemory(Device, &ImgAllocInfo, nullptr, &ImgMemory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindImageMemory(Device, Img, ImgMemory, 0), VK_SUCCESS);
  VkImageViewCreateInfo ImgViewInfo{};
  ImgViewInfo.image = Img;
  ImgViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  ImgViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ImgViewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkImageView ImgView = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateImageView(Device, &ImgViewInfo, nullptr, &ImgView),
            VK_SUCCESS);
  VkSamplerCreateInfo SamplerInfo{};
  VkSampler Samp = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateSampler(Device, &SamplerInfo, nullptr, &Samp), VK_SUCCESS);

  VkDescriptorPoolSize PoolSizes[2] = {
      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
  };
  VkDescriptorPoolCreateInfo PoolInfo{};
  PoolInfo.maxSets = 1;
  PoolInfo.poolSizeCount = 2;
  PoolInfo.pPoolSizes = PoolSizes;
  VkDescriptorPool DescPool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &DescPool),
            VK_SUCCESS);
  VkDescriptorSetAllocateInfo DSAllocInfo{};
  DSAllocInfo.descriptorPool = DescPool;
  DSAllocInfo.descriptorSetCount = 1;
  DSAllocInfo.pSetLayouts = &SetLayout;
  VkDescriptorSet Set = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateDescriptorSets(Device, &DSAllocInfo, &Set), VK_SUCCESS);

  VkDescriptorImageInfo ImageInfoWrite{};
  ImageInfoWrite.sampler = Samp;
  ImageInfoWrite.imageView = ImgView;
  ImageInfoWrite.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkWriteDescriptorSet Writes[2]{};
  Writes[0].dstSet = Set;
  Writes[0].dstBinding = 0;
  Writes[0].descriptorCount = 1;
  Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
  Writes[0].pTexelBufferView = &View;
  Writes[1].dstSet = Set;
  Writes[1].dstBinding = 1;
  Writes[1].descriptorCount = 1;
  Writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  Writes[1].pImageInfo = &ImageInfoWrite;
  vkUpdateDescriptorSets(Device, 2, Writes, 0, nullptr);

  VkCommandBufferAllocateInfo CmdAllocInfo{};
  CmdAllocInfo.commandPool = Pool;
  CmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  CmdAllocInfo.commandBufferCount = 1;
  VkCommandBuffer TestCmdBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &CmdAllocInfo, &TestCmdBuf),
            VK_SUCCESS);
  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(TestCmdBuf, &BeginInfo), VK_SUCCESS);
  vkCmdBindDescriptorSets(TestCmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, PLayout,
                         0, 1, &Set, 0, nullptr);
  ASSERT_EQ(vkEndCommandBuffer(TestCmdBuf), VK_SUCCESS);

  VkSubmitInfo Submit{};
  Submit.commandBufferCount = 1;
  Submit.pCommandBuffers = &TestCmdBuf;
  ASSERT_EQ(vkQueueSubmit(Queue, 1, &Submit, VK_NULL_HANDLE), VK_SUCCESS);

  // The hazardous ordering: free the `DescriptorSet` (which also destroys
  // its pool via `vkResetDescriptorPool`, exercising `DescriptorPool`'s own
  // guard) and destroy the `BufferView`/`Sampler`/`ImageView`/`Image`/
  // `Buffer` it has bound -- all with no wait at all, right after the
  // just-enqueued submission references them.
  vkResetDescriptorPool(Device, DescPool, 0);
  vkDestroyBufferView(Device, View, nullptr);
  vkDestroySampler(Device, Samp, nullptr);
  vkDestroyImageView(Device, ImgView, nullptr);
  vkDestroyImage(Device, Img, nullptr);
  vkDestroyBuffer(Device, Buf, nullptr);

  // Only now wait for the submission to actually finish -- by this point,
  // pre-fix, the worker thread may already have run past (or crashed on)
  // its read of the freed objects.
  EXPECT_EQ(vkQueueWaitIdle(Queue), VK_SUCCESS);

  vkFreeMemory(Device, BufMemory, nullptr);
  vkFreeMemory(Device, ImgMemory, nullptr);
  vkFreeCommandBuffers(Device, Pool, 1, &TestCmdBuf);
  vkDestroyDescriptorPool(Device, DescPool, nullptr);
  vkDestroyPipelineLayout(Device, PLayout, nullptr);
  vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
}

// (Roadmap L410) `Event`'s analog of
// `SubmitThenImmediatelyDestroyBufferWithNoWaitSucceeds` above: the same
// hazardous ordering, but for an `Event` referenced via `vkCmdSetEvent`/
// `vkCmdWaitEvents`.
TEST_F(SyncTest, SubmitThenImmediatelyDestroyEventWithNoWaitSucceeds) {
  VkEventCreateInfo EventInfo{};
  VkEvent Ev = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateEvent(Device, &EventInfo, nullptr, &Ev), VK_SUCCESS);

  VkCommandBufferAllocateInfo CmdAllocInfo{};
  CmdAllocInfo.commandPool = Pool;
  CmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  CmdAllocInfo.commandBufferCount = 1;
  VkCommandBuffer TestCmdBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &CmdAllocInfo, &TestCmdBuf),
            VK_SUCCESS);
  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(TestCmdBuf, &BeginInfo), VK_SUCCESS);
  vkCmdSetEvent(TestCmdBuf, Ev, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
  ASSERT_EQ(vkEndCommandBuffer(TestCmdBuf), VK_SUCCESS);

  VkSubmitInfo Submit{};
  Submit.commandBufferCount = 1;
  Submit.pCommandBuffers = &TestCmdBuf;
  ASSERT_EQ(vkQueueSubmit(Queue, 1, &Submit, VK_NULL_HANDLE), VK_SUCCESS);

  // The hazardous ordering: destroy the `VkEvent` wrapper object the
  // just-enqueued submission references with no wait at all.
  vkDestroyEvent(Device, Ev, nullptr);

  // Only now wait for the submission to actually finish -- by this point,
  // pre-fix, the worker thread may already have run past (or crashed on)
  // its read of the freed `Event` object.
  EXPECT_EQ(vkQueueWaitIdle(Queue), VK_SUCCESS);

  vkFreeCommandBuffers(Device, Pool, 1, &TestCmdBuf);
}

// (Roadmap L410) `QueryPool`'s analog of
// `SubmitThenImmediatelyDestroyBufferWithNoWaitSucceeds` above: the same
// hazardous ordering, but for a `QueryPool` referenced via
// `vkCmdBeginQuery`/`vkCmdEndQuery`.
TEST_F(SyncTest, SubmitThenImmediatelyDestroyQueryPoolWithNoWaitSucceeds) {
  VkQueryPoolCreateInfo QueryPoolInfo{};
  QueryPoolInfo.queryType = VK_QUERY_TYPE_OCCLUSION;
  QueryPoolInfo.queryCount = 1;
  VkQueryPool QP = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateQueryPool(Device, &QueryPoolInfo, nullptr, &QP),
            VK_SUCCESS);

  VkCommandBufferAllocateInfo CmdAllocInfo{};
  CmdAllocInfo.commandPool = Pool;
  CmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  CmdAllocInfo.commandBufferCount = 1;
  VkCommandBuffer TestCmdBuf = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &CmdAllocInfo, &TestCmdBuf),
            VK_SUCCESS);
  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(TestCmdBuf, &BeginInfo), VK_SUCCESS);
  vkCmdResetQueryPool(TestCmdBuf, QP, 0, 1);
  vkCmdBeginQuery(TestCmdBuf, QP, 0, 0);
  vkCmdEndQuery(TestCmdBuf, QP, 0);
  ASSERT_EQ(vkEndCommandBuffer(TestCmdBuf), VK_SUCCESS);

  VkSubmitInfo Submit{};
  Submit.commandBufferCount = 1;
  Submit.pCommandBuffers = &TestCmdBuf;
  ASSERT_EQ(vkQueueSubmit(Queue, 1, &Submit, VK_NULL_HANDLE), VK_SUCCESS);

  // The hazardous ordering: destroy the `VkQueryPool` wrapper object the
  // just-enqueued submission references with no wait at all.
  vkDestroyQueryPool(Device, QP, nullptr);

  // Only now wait for the submission to actually finish -- by this point,
  // pre-fix, the worker thread may already have run past (or crashed on)
  // its read of the freed `QueryPool` object.
  EXPECT_EQ(vkQueueWaitIdle(Queue), VK_SUCCESS);

  vkFreeCommandBuffers(Device, Pool, 1, &TestCmdBuf);
}
