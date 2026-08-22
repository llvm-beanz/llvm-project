//===- PipelineCacheTest.cpp - VkPipelineCache tests ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "PipelineCache.h"
#include "Descriptor.h"
#include "EntryPoints.h"
#include "Icd.h"
#include "Objects.h"
#include "Pipeline.h"

#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Target/SPIRV/Serialization.h"
#include "llvm/ADT/SmallVector.h"

#include "gtest/gtest.h"

#include <cstring>
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

class PipelineCacheTest : public ::testing::Test {
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
  }
  void TearDown() override {
    vkDestroyShaderModule(Device, Module, nullptr);
    vkDestroyPipelineLayout(Device, Layout, nullptr);
    vkDestroyDevice(Device, nullptr);
    vkDestroyInstance(Instance, nullptr);
  }

  VkComputePipelineCreateInfo makeCreateInfo() const {
    VkComputePipelineCreateInfo CreateInfo{};
    CreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    CreateInfo.stage.module = Module;
    CreateInfo.stage.pName = "main";
    CreateInfo.layout = Layout;
    return CreateInfo;
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
  VkPipelineLayout Layout = VK_NULL_HANDLE;
  VkShaderModule Module = VK_NULL_HANDLE;
};

TEST_F(PipelineCacheTest, CreateAndDestroyEmptyPipelineCache) {
  VkPipelineCacheCreateInfo CacheInfo{};
  VkPipelineCache Cache = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreatePipelineCache(Device, &CacheInfo, nullptr, &Cache),
            VK_SUCCESS);
  EXPECT_NE(Cache, VK_NULL_HANDLE);
  vkDestroyPipelineCache(Device, Cache, nullptr);
}

TEST_F(PipelineCacheTest, CachedPipelineIsSharedAcrossCreations) {
  VkPipelineCacheCreateInfo CacheInfo{};
  VkPipelineCache Cache = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineCache(Device, &CacheInfo, nullptr, &Cache),
            VK_SUCCESS);

  VkComputePipelineCreateInfo CreateInfo = makeCreateInfo();
  VkPipeline First = VK_NULL_HANDLE, Second = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreateComputePipelines(Device, Cache, 1, &CreateInfo, nullptr, &First),
      VK_SUCCESS);
  ASSERT_EQ(
      vkCreateComputePipelines(Device, Cache, 1, &CreateInfo, nullptr, &Second),
      VK_SUCCESS);

  // A cache hit shares the same underlying compiled artifact rather than
  // recompiling -- see "PipelineCache.h"'s file comment.
  EXPECT_EQ(&fromHandle<ComputePipeline>(First)->getStage(),
            &fromHandle<ComputePipeline>(Second)->getStage());

  vkDestroyPipeline(Device, First, nullptr);
  vkDestroyPipeline(Device, Second, nullptr);
  vkDestroyPipelineCache(Device, Cache, nullptr);
}

/// Roadmap E19 (`VK_EXT_pipeline_creation_feedback`): the first creation
/// misses the cache (no `APPLICATION_PIPELINE_CACHE_HIT_BIT`), the second
/// (identical) creation hits it -- exercising
/// `fillPipelineCreationFeedback`'s (Pipeline.cpp) two different flag
/// outcomes against the exact cache-hit/-miss pair
/// `CachedPipelineIsSharedAcrossCreations` above already establishes.
TEST_F(PipelineCacheTest, CreationFeedbackReportsCacheHitOnSecondCreation) {
  VkPipelineCacheCreateInfo CacheInfo{};
  VkPipelineCache Cache = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineCache(Device, &CacheInfo, nullptr, &Cache),
            VK_SUCCESS);

  VkPipelineCreationFeedback Feedback{};
  VkPipelineCreationFeedbackCreateInfo FeedbackInfo{};
  FeedbackInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO;
  FeedbackInfo.pPipelineCreationFeedback = &Feedback;

  VkComputePipelineCreateInfo CreateInfo = makeCreateInfo();
  CreateInfo.pNext = &FeedbackInfo;
  VkPipeline First = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreateComputePipelines(Device, Cache, 1, &CreateInfo, nullptr, &First),
      VK_SUCCESS);
  EXPECT_TRUE(Feedback.flags & VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT);
  EXPECT_FALSE(Feedback.flags &
               VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT);

  Feedback = VkPipelineCreationFeedback{};
  VkPipeline Second = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreateComputePipelines(Device, Cache, 1, &CreateInfo, nullptr, &Second),
      VK_SUCCESS);
  EXPECT_TRUE(Feedback.flags & VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT);
  EXPECT_TRUE(Feedback.flags &
              VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT);

  vkDestroyPipeline(Device, First, nullptr);
  vkDestroyPipeline(Device, Second, nullptr);
  vkDestroyPipelineCache(Device, Cache, nullptr);
}

TEST_F(PipelineCacheTest, NoCacheCompilesIndependentArtifactsEachTime) {
  VkComputePipelineCreateInfo CreateInfo = makeCreateInfo();
  VkPipeline First = VK_NULL_HANDLE, Second = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &First),
            VK_SUCCESS);
  ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Second),
            VK_SUCCESS);

  EXPECT_NE(&fromHandle<ComputePipeline>(First)->getStage(),
            &fromHandle<ComputePipeline>(Second)->getStage());

  vkDestroyPipeline(Device, First, nullptr);
  vkDestroyPipeline(Device, Second, nullptr);
}

/// Roadmap E7: two otherwise-identical creations that disagree only in
/// their chained `VkPipelineShaderStageRequiredSubgroupSizeCreateInfo` must
/// not collide on the same cached artifact -- `computePipelineCacheKey`
/// folds `requiredSubgroupSize` in for exactly this reason.
TEST_F(PipelineCacheTest,
       DifferentRequiredSubgroupSizesAreNotSharedAcrossCacheEntries) {
  VkPipelineCacheCreateInfo CacheInfo{};
  VkPipelineCache Cache = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineCache(Device, &CacheInfo, nullptr, &Cache),
            VK_SUCCESS);

  VkPipelineShaderStageRequiredSubgroupSizeCreateInfo RequiredSizeFour{};
  RequiredSizeFour.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO;
  RequiredSizeFour.requiredSubgroupSize = 4;
  VkComputePipelineCreateInfo CreateInfoFour = makeCreateInfo();
  CreateInfoFour.stage.pNext = &RequiredSizeFour;

  VkPipelineShaderStageRequiredSubgroupSizeCreateInfo RequiredSizeEight{};
  RequiredSizeEight.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO;
  RequiredSizeEight.requiredSubgroupSize = 8;
  VkComputePipelineCreateInfo CreateInfoEight = makeCreateInfo();
  CreateInfoEight.stage.pNext = &RequiredSizeEight;

  VkPipeline First = VK_NULL_HANDLE, Second = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, Cache, 1, &CreateInfoFour, nullptr,
                                     &First),
            VK_SUCCESS);
  ASSERT_EQ(vkCreateComputePipelines(Device, Cache, 1, &CreateInfoEight,
                                     nullptr, &Second),
            VK_SUCCESS);

  EXPECT_NE(&fromHandle<ComputePipeline>(First)->getStage(),
            &fromHandle<ComputePipeline>(Second)->getStage());

  // A third, identical-to-`First` creation still hits, though.
  VkPipeline Third = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, Cache, 1, &CreateInfoFour, nullptr,
                                     &Third),
            VK_SUCCESS);
  EXPECT_EQ(&fromHandle<ComputePipeline>(First)->getStage(),
            &fromHandle<ComputePipeline>(Third)->getStage());

  vkDestroyPipeline(Device, First, nullptr);
  vkDestroyPipeline(Device, Second, nullptr);
  vkDestroyPipeline(Device, Third, nullptr);
  vkDestroyPipelineCache(Device, Cache, nullptr);
}

TEST_F(PipelineCacheTest, DataRoundTripsThroughANewCache) {
  VkPipelineCacheCreateInfo CacheInfo{};
  VkPipelineCache Cache = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineCache(Device, &CacheInfo, nullptr, &Cache),
            VK_SUCCESS);

  VkComputePipelineCreateInfo CreateInfo = makeCreateInfo();
  VkPipeline Pipeline = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, Cache, 1, &CreateInfo, nullptr,
                                     &Pipeline),
            VK_SUCCESS);

  size_t DataSize = 0;
  ASSERT_EQ(vkGetPipelineCacheData(Device, Cache, &DataSize, nullptr),
            VK_SUCCESS);
  ASSERT_GT(DataSize, 0u);
  std::vector<uint8_t> Data(DataSize);
  ASSERT_EQ(vkGetPipelineCacheData(Device, Cache, &DataSize, Data.data()),
            VK_SUCCESS);

  VkPipelineCacheCreateInfo ReloadedInfo{};
  ReloadedInfo.initialDataSize = Data.size();
  ReloadedInfo.pInitialData = Data.data();
  VkPipelineCache Reloaded = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineCache(Device, &ReloadedInfo, nullptr, &Reloaded),
            VK_SUCCESS);

  // The reloaded cache knows the same key, so a subsequent identical
  // creation is (at least) not rejected -- whether it is an actual hit
  // depends on whether this process still holds the compiled artifact,
  // which `PipelineCache::lookup` correctly reports as a miss for a
  // key-only (no artifact) entry loaded from a blob (see the header
  // comment: persistent data records keys, not relocatable object code).
  VkPipeline FromReloaded = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateComputePipelines(Device, Reloaded, 1, &CreateInfo, nullptr,
                                     &FromReloaded),
            VK_SUCCESS);
  EXPECT_NE(FromReloaded, VK_NULL_HANDLE);

  vkDestroyPipeline(Device, FromReloaded, nullptr);
  vkDestroyPipeline(Device, Pipeline, nullptr);
  vkDestroyPipelineCache(Device, Reloaded, nullptr);
  vkDestroyPipelineCache(Device, Cache, nullptr);
}

TEST_F(PipelineCacheTest, TamperedDataIsTreatedAsAnEmptyCacheNotAnError) {
  VkPipelineCacheCreateInfo CacheInfo{};
  VkPipelineCache Cache = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineCache(Device, &CacheInfo, nullptr, &Cache),
            VK_SUCCESS);
  VkComputePipelineCreateInfo CreateInfo = makeCreateInfo();
  VkPipeline Pipeline = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, Cache, 1, &CreateInfo, nullptr,
                                     &Pipeline),
            VK_SUCCESS);

  size_t DataSize = 0;
  ASSERT_EQ(vkGetPipelineCacheData(Device, Cache, &DataSize, nullptr),
            VK_SUCCESS);
  std::vector<uint8_t> Data(DataSize);
  ASSERT_EQ(vkGetPipelineCacheData(Device, Cache, &DataSize, Data.data()),
            VK_SUCCESS);
  // Flip a byte inside the (digest-covered) key list -- the digest check
  // must catch this, not just the header fields.
  ASSERT_GT(Data.size(), 40u);
  Data[40] ^= 0xFF;

  VkPipelineCacheCreateInfo TamperedInfo{};
  TamperedInfo.initialDataSize = Data.size();
  TamperedInfo.pInitialData = Data.data();
  VkPipelineCache Tampered = VK_NULL_HANDLE;
  // Per "Pipeline Cache": "treat any validation failure as an empty cache,
  // never as an error" -- vkCreatePipelineCache itself must still succeed.
  EXPECT_EQ(vkCreatePipelineCache(Device, &TamperedInfo, nullptr, &Tampered),
            VK_SUCCESS);
  EXPECT_NE(Tampered, VK_NULL_HANDLE);

  vkDestroyPipelineCache(Device, Tampered, nullptr);
  vkDestroyPipeline(Device, Pipeline, nullptr);
  vkDestroyPipelineCache(Device, Cache, nullptr);
}

TEST_F(PipelineCacheTest, MergePipelineCachesAdoptsSourceKeys) {
  VkPipelineCacheCreateInfo CacheInfo{};
  VkPipelineCache Src = VK_NULL_HANDLE, Dst = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineCache(Device, &CacheInfo, nullptr, &Src),
            VK_SUCCESS);
  ASSERT_EQ(vkCreatePipelineCache(Device, &CacheInfo, nullptr, &Dst),
            VK_SUCCESS);

  VkComputePipelineCreateInfo CreateInfo = makeCreateInfo();
  VkPipeline Pipeline = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreateComputePipelines(Device, Src, 1, &CreateInfo, nullptr, &Pipeline),
      VK_SUCCESS);

  ASSERT_EQ(vkMergePipelineCaches(Device, Dst, 1, &Src), VK_SUCCESS);

  VkPipeline FromDst = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreateComputePipelines(Device, Dst, 1, &CreateInfo, nullptr, &FromDst),
      VK_SUCCESS);
  // Dst adopted Src's already-compiled artifact, so this is a real hit.
  EXPECT_EQ(&fromHandle<ComputePipeline>(Pipeline)->getStage(),
            &fromHandle<ComputePipeline>(FromDst)->getStage());

  vkDestroyPipeline(Device, Pipeline, nullptr);
  vkDestroyPipeline(Device, FromDst, nullptr);
  vkDestroyPipelineCache(Device, Src, nullptr);
  vkDestroyPipelineCache(Device, Dst, nullptr);
}

/// Roadmap E9: `VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT` must be
/// accepted, and a cache created with it must still behave like any other
/// (single-threaded) cache -- the bit only ever changes whether this ICD's
/// own internal locking runs, never the cache's externally observable
/// lookup/insert behavior.
TEST_F(PipelineCacheTest, ExternallySynchronizedCacheStillHitsNormally) {
  VkPipelineCacheCreateInfo CacheInfo{};
  CacheInfo.flags = VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;
  VkPipelineCache Cache = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineCache(Device, &CacheInfo, nullptr, &Cache),
            VK_SUCCESS);

  VkComputePipelineCreateInfo CreateInfo = makeCreateInfo();
  VkPipeline First = VK_NULL_HANDLE, Second = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreateComputePipelines(Device, Cache, 1, &CreateInfo, nullptr, &First),
      VK_SUCCESS);
  ASSERT_EQ(
      vkCreateComputePipelines(Device, Cache, 1, &CreateInfo, nullptr, &Second),
      VK_SUCCESS);
  EXPECT_EQ(&fromHandle<ComputePipeline>(First)->getStage(),
            &fromHandle<ComputePipeline>(Second)->getStage());

  vkDestroyPipeline(Device, First, nullptr);
  vkDestroyPipeline(Device, Second, nullptr);
  vkDestroyPipelineCache(Device, Cache, nullptr);
}

/// Roadmap E9: without `VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT`
/// (the default), `vkCreateComputePipelines` must tolerate concurrent
/// callers sharing one `VkPipelineCache` -- `pipelineCache` is not one of
/// its own externally-synchronized parameters (see PipelineCache.h's class
/// comment). This does not prove the absence of a race on its own, but
/// exercises the same code path a thread sanitizer build would need to
/// catch a regression here.
TEST_F(PipelineCacheTest,
       ConcurrentCreationsAgainstOneCacheAreThreadSafeByDefault) {
  VkPipelineCacheCreateInfo CacheInfo{};
  VkPipelineCache Cache = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineCache(Device, &CacheInfo, nullptr, &Cache),
            VK_SUCCESS);

  VkComputePipelineCreateInfo CreateInfo = makeCreateInfo();
  constexpr int kThreads = 8;
  std::vector<std::thread> Threads;
  std::vector<VkPipeline> Pipelines(kThreads, VK_NULL_HANDLE);
  std::vector<VkResult> Results(kThreads, VK_ERROR_UNKNOWN);
  for (int I = 0; I != kThreads; ++I) {
    Threads.emplace_back([&, I] {
      Results[I] = vkCreateComputePipelines(Device, Cache, 1, &CreateInfo,
                                            nullptr, &Pipelines[I]);
    });
  }
  for (std::thread &T : Threads)
    T.join();

  for (int I = 0; I != kThreads; ++I) {
    EXPECT_EQ(Results[I], VK_SUCCESS);
    EXPECT_NE(Pipelines[I], VK_NULL_HANDLE);
  }
  for (VkPipeline P : Pipelines)
    if (P)
      vkDestroyPipeline(Device, P, nullptr);
  vkDestroyPipelineCache(Device, Cache, nullptr);
}

/// Roadmap E9: `VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT`
/// with no cache at all -- there is never a hit to have, so creation must
/// always report `VK_PIPELINE_COMPILE_REQUIRED` and leave the pipeline
/// null rather than compile it for real.
TEST_F(PipelineCacheTest, FailOnCompileRequiredWithNoCacheAlwaysFails) {
  VkComputePipelineCreateInfo CreateInfo = makeCreateInfo();
  CreateInfo.flags = VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;
  VkPipeline Pipeline = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Pipeline),
            VK_PIPELINE_COMPILE_REQUIRED);
  EXPECT_EQ(Pipeline, VK_NULL_HANDLE);
}

/// Roadmap E9: with a cache, a first creation carrying
/// `VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT` misses (the
/// cache starts empty) and must fail without populating the cache; a
/// second, ordinary creation actually compiles and populates it; a third
/// creation with the bit set again now hits and must succeed, reusing the
/// artifact the second creation inserted.
TEST_F(PipelineCacheTest, FailOnCompileRequiredSucceedsOnceCachePopulated) {
  VkPipelineCacheCreateInfo CacheInfo{};
  VkPipelineCache Cache = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineCache(Device, &CacheInfo, nullptr, &Cache),
            VK_SUCCESS);

  VkComputePipelineCreateInfo NoCompileInfo = makeCreateInfo();
  NoCompileInfo.flags =
      VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;
  VkPipeline Missed = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateComputePipelines(Device, Cache, 1, &NoCompileInfo, nullptr,
                                     &Missed),
            VK_PIPELINE_COMPILE_REQUIRED);
  EXPECT_EQ(Missed, VK_NULL_HANDLE);

  VkComputePipelineCreateInfo NormalInfo = makeCreateInfo();
  VkPipeline Compiled = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, Cache, 1, &NormalInfo, nullptr,
                                     &Compiled),
            VK_SUCCESS);
  ASSERT_NE(Compiled, VK_NULL_HANDLE);

  VkPipeline Hit = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, Cache, 1, &NoCompileInfo, nullptr,
                                     &Hit),
            VK_SUCCESS);
  ASSERT_NE(Hit, VK_NULL_HANDLE);
  EXPECT_EQ(&fromHandle<ComputePipeline>(Compiled)->getStage(),
            &fromHandle<ComputePipeline>(Hit)->getStage());

  vkDestroyPipeline(Device, Compiled, nullptr);
  vkDestroyPipeline(Device, Hit, nullptr);
  vkDestroyPipelineCache(Device, Cache, nullptr);
}

/// Roadmap E9: in a batch, a real compile failure elsewhere must still be
/// reported (`VK_ERROR_INITIALIZATION_FAILED`) rather than masked by a
/// sibling's `VK_PIPELINE_COMPILE_REQUIRED` -- the more severe result wins,
/// per the extension's own spec.
TEST_F(PipelineCacheTest, RealFailureOutranksPipelineCompileRequired) {
  VkComputePipelineCreateInfo NoCompileInfo = makeCreateInfo();
  NoCompileInfo.flags =
      VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;

  VkComputePipelineCreateInfo BadInfo = makeCreateInfo();
  BadInfo.layout = VK_NULL_HANDLE; // Invalid: compilation always fails.

  VkComputePipelineCreateInfo CreateInfos[] = {NoCompileInfo, BadInfo};
  VkPipeline Pipelines[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
  EXPECT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 2, CreateInfos,
                                     nullptr, Pipelines),
            VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Pipelines[0], VK_NULL_HANDLE);
  EXPECT_EQ(Pipelines[1], VK_NULL_HANDLE);
}

} // namespace
