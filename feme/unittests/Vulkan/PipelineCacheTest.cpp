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
#include "GraphicsPipeline.h"
#include "Icd.h"
#include "Objects.h"
#include "Pipeline.h"

#include "feme/Core/Context.h"
#include "feme/Target/CPU/CompiledStage.h"

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

/// Roadmap L89c: with no `VkPipelineCache` at all, two identical creations
/// must still share one compiled artifact, via the device's implicit cache
/// -- the whole point of that cache, since a JIT-compiling ICD cannot
/// afford to recompile an identical shader just because the app never
/// opted into a cache object.
TEST_F(PipelineCacheTest, NoCacheStillSharesArtifactViaImplicitCache) {
  VkComputePipelineCreateInfo CreateInfo = makeCreateInfo();
  VkPipeline First = VK_NULL_HANDLE, Second = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &First),
            VK_SUCCESS);
  ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Second),
            VK_SUCCESS);

  EXPECT_EQ(&fromHandle<ComputePipeline>(First)->getStage(),
            &fromHandle<ComputePipeline>(Second)->getStage());

  vkDestroyPipeline(Device, First, nullptr);
  vkDestroyPipeline(Device, Second, nullptr);
}

/// Roadmap L89c: the implicit cache must not make two *different* shaders
/// collide -- it is keyed by exactly the same `computePipelineCacheKey` an
/// app-supplied cache is, so a creation differing in any keyed input still
/// compiles its own artifact.
TEST_F(PipelineCacheTest, ImplicitCacheDoesNotShareAcrossDifferentKeys) {
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

  VkPipeline Four = VK_NULL_HANDLE, Eight = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfoFour,
                                     nullptr, &Four),
            VK_SUCCESS);
  ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1,
                                     &CreateInfoEight, nullptr, &Eight),
            VK_SUCCESS);

  EXPECT_NE(&fromHandle<ComputePipeline>(Four)->getStage(),
            &fromHandle<ComputePipeline>(Eight)->getStage());

  vkDestroyPipeline(Device, Four, nullptr);
  vkDestroyPipeline(Device, Eight, nullptr);
}

/// Roadmap L89c: an *implicit* cache hit is not an application cache hit.
/// `VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT`
/// specifically reports that the app's own `VkPipelineCache` supplied the
/// pipeline, so a creation that passed no cache at all must never set it,
/// however the implementation actually satisfied the request.
TEST_F(PipelineCacheTest, ImplicitCacheHitIsNotReportedAsApplicationCacheHit) {
  VkComputePipelineCreateInfo First = makeCreateInfo();
  VkPipeline Warm = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &First, nullptr,
                                     &Warm),
            VK_SUCCESS);

  VkPipelineCreationFeedback Feedback{};
  VkPipelineCreationFeedbackCreateInfo FeedbackInfo{};
  FeedbackInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO;
  FeedbackInfo.pPipelineCreationFeedback = &Feedback;
  VkComputePipelineCreateInfo Second = makeCreateInfo();
  Second.pNext = &FeedbackInfo;

  VkPipeline Hit = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &Second,
                                     nullptr, &Hit),
            VK_SUCCESS);
  EXPECT_TRUE(Feedback.flags & VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT);
  EXPECT_FALSE(
      Feedback.flags &
      VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT);

  vkDestroyPipeline(Device, Warm, nullptr);
  vkDestroyPipeline(Device, Hit, nullptr);
}

/// Roadmap L89c: an app-supplied cache that misses, but whose key the
/// implicit cache already knows, must adopt that artifact -- so a *later*
/// creation through the same app cache reports the application-cache hit
/// the app is entitled to expect, rather than perpetually missing because
/// the implicit cache silently answered every request first.
TEST_F(PipelineCacheTest, ImplicitHitPopulatesTheApplicationCache) {
  VkComputePipelineCreateInfo Warmup = makeCreateInfo();
  VkPipeline Warm = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &Warmup,
                                     nullptr, &Warm),
            VK_SUCCESS);

  VkPipelineCacheCreateInfo CacheInfo{};
  VkPipelineCache Cache = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineCache(Device, &CacheInfo, nullptr, &Cache),
            VK_SUCCESS);

  VkComputePipelineCreateInfo Adopting = makeCreateInfo();
  VkPipeline Adopted = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreateComputePipelines(Device, Cache, 1, &Adopting, nullptr, &Adopted),
      VK_SUCCESS);

  VkPipelineCreationFeedback Feedback{};
  VkPipelineCreationFeedbackCreateInfo FeedbackInfo{};
  FeedbackInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO;
  FeedbackInfo.pPipelineCreationFeedback = &Feedback;
  VkComputePipelineCreateInfo Reported = makeCreateInfo();
  Reported.pNext = &FeedbackInfo;
  VkPipeline Hit = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreateComputePipelines(Device, Cache, 1, &Reported, nullptr, &Hit),
      VK_SUCCESS);
  EXPECT_TRUE(Feedback.flags &
              VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT);

  vkDestroyPipeline(Device, Warm, nullptr);
  vkDestroyPipeline(Device, Adopted, nullptr);
  vkDestroyPipeline(Device, Hit, nullptr);
  vkDestroyPipelineCache(Device, Cache, nullptr);
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
/// with no cache at all, against a cold device -- neither the app (which
/// supplied none) nor the implicit cache (roadmap L89c, still empty on a
/// freshly created device) can satisfy this, so a real compile *is*
/// required and creation must report `VK_PIPELINE_COMPILE_REQUIRED` and
/// leave the pipeline null rather than compile it.
TEST_F(PipelineCacheTest, FailOnCompileRequiredWithNoCacheFailsWhenCold) {
  VkComputePipelineCreateInfo CreateInfo = makeCreateInfo();
  CreateInfo.flags = VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;
  VkPipeline Pipeline = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Pipeline),
            VK_PIPELINE_COMPILE_REQUIRED);
  EXPECT_EQ(Pipeline, VK_NULL_HANDLE);
}

/// Roadmap L89c: once the implicit cache is warm for this exact key, a
/// creation carrying `VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_
/// BIT` and *no* app cache must now succeed: the bit asks the
/// implementation not to compile, and no compile is needed to satisfy the
/// request, which is precisely the case the bit exists to let an app
/// exploit.
TEST_F(PipelineCacheTest, FailOnCompileRequiredSucceedsOnImplicitCacheHit) {
  VkComputePipelineCreateInfo Warmup = makeCreateInfo();
  VkPipeline Warm = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &Warmup,
                                     nullptr, &Warm),
            VK_SUCCESS);

  VkComputePipelineCreateInfo NoCompileInfo = makeCreateInfo();
  NoCompileInfo.flags =
      VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;
  VkPipeline Hit = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &NoCompileInfo,
                                     nullptr, &Hit),
            VK_SUCCESS);
  EXPECT_NE(Hit, VK_NULL_HANDLE);

  vkDestroyPipeline(Device, Warm, nullptr);
  vkDestroyPipeline(Device, Hit, nullptr);
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

/// Roadmap L94(g): `VK_EXT_graphics_pipeline_library`'s independent-sets
/// feature allows an unused descriptor set's layout to be `VK_NULL_HANDLE`
/// (`VkPipelineLayoutCreateInfo::pSetLayouts[I]`), which `fromHandle` maps
/// to a `nullptr` `DescriptorSetLayout *`. `computeGraphicsPipelineCacheKey`
/// previously dereferenced every set layout unconditionally, so hashing a
/// layout with such an unused set crashed rather than treating it as
/// contributing no bindings.
TEST_F(PipelineCacheTest, ComputePipelineCacheKeyToleratesNullSetLayout) {
  VkDescriptorSetLayout SetLayouts[] = {VK_NULL_HANDLE};
  VkPipelineLayoutCreateInfo LayoutInfo{};
  LayoutInfo.setLayoutCount = 1;
  LayoutInfo.pSetLayouts = SetLayouts;
  VkPipelineLayout LayoutWithNullSet = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &LayoutWithNullSet),
      VK_SUCCESS);

  VkComputePipelineCreateInfo CreateInfo = makeCreateInfo();
  CreateInfo.layout = LayoutWithNullSet;
  VkPipeline Pipeline = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Pipeline),
            VK_SUCCESS);

  vkDestroyPipeline(Device, Pipeline, nullptr);
  vkDestroyPipelineLayout(Device, LayoutWithNullSet, nullptr);
}

} // namespace

namespace {

PipelineCacheKey makeKey(uint8_t Seed) {
  PipelineCacheKey Key{};
  Key[0] = Seed;
  return Key;
}

/// A distinct, cheap artifact identity for the bound tests below: both
/// members may legitimately be null, so this needs no real compile.
std::shared_ptr<CachedPipelineArtifact> makeArtifact() {
  return std::make_shared<CachedPipelineArtifact>();
}

} // namespace

/// Roadmap L89c: an unbounded cache -- what every app-created
/// `VkPipelineCache` is, and what this class did exclusively before the
/// implicit cache existed -- never evicts, however many entries it takes.
TEST(PipelineCacheBoundTest, UnboundedCacheNeverEvicts) {
  PipelineCache Cache;
  for (uint8_t I = 0; I != 32; ++I)
    Cache.insert(makeKey(I), makeArtifact());

  for (uint8_t I = 0; I != 32; ++I)
    EXPECT_NE(Cache.lookup(makeKey(I)), nullptr) << "entry " << unsigned(I);
  EXPECT_EQ(Cache.keys().size(), 32u);
}

/// Roadmap L89c: a bounded cache retains its most recent `MaxEntries`
/// insertions and drops the oldest, so an always-on implicit cache cannot
/// grow without limit over a long-lived device.
TEST(PipelineCacheBoundTest, BoundedCacheEvictsOldestInsertions) {
  PipelineCache Cache(/*InitialKeys=*/{}, /*ExternallySynchronized=*/false,
                      /*MaxEntries=*/4);
  for (uint8_t I = 0; I != 10; ++I)
    Cache.insert(makeKey(I), makeArtifact());

  EXPECT_EQ(Cache.keys().size(), 4u);
  for (uint8_t I = 0; I != 6; ++I)
    EXPECT_EQ(Cache.lookup(makeKey(I)), nullptr) << "evicted " << unsigned(I);
  for (uint8_t I = 6; I != 10; ++I)
    EXPECT_NE(Cache.lookup(makeKey(I)), nullptr) << "retained " << unsigned(I);
}

/// Roadmap L89c: re-inserting a key already present must refresh it in
/// place rather than count as a second insertion -- otherwise the eviction
/// queue would fill with duplicates and evict live entries early.
TEST(PipelineCacheBoundTest, ReinsertingAKeyDoesNotConsumeExtraCapacity) {
  PipelineCache Cache(/*InitialKeys=*/{}, /*ExternallySynchronized=*/false,
                      /*MaxEntries=*/2);
  Cache.insert(makeKey(1), makeArtifact());
  for (unsigned I = 0; I != 8; ++I)
    Cache.insert(makeKey(2), makeArtifact());

  EXPECT_EQ(Cache.keys().size(), 2u);
  EXPECT_NE(Cache.lookup(makeKey(1)), nullptr);
  EXPECT_NE(Cache.lookup(makeKey(2)), nullptr);
}

/// Roadmap L89c: the graphics table is bounded independently of the
/// compute one -- they are separate tables, so filling one must not evict
/// the other's entries.
TEST(PipelineCacheBoundTest, ComputeAndGraphicsTablesAreBoundedSeparately) {
  PipelineCache Cache(/*InitialKeys=*/{}, /*ExternallySynchronized=*/false,
                      /*MaxEntries=*/2);
  Cache.insert(makeKey(1), makeArtifact());
  Cache.insert(makeKey(2), makeArtifact());
  for (uint8_t I = 10; I != 20; ++I)
    Cache.insertGraphics(makeKey(I),
                         std::make_shared<GraphicsPipelineArtifact>());

  EXPECT_NE(Cache.lookup(makeKey(1)), nullptr);
  EXPECT_NE(Cache.lookup(makeKey(2)), nullptr);
  EXPECT_NE(Cache.lookupGraphics(makeKey(19)), nullptr);
  EXPECT_EQ(Cache.lookupGraphics(makeKey(10)), nullptr);
}

/// Roadmap L89c: `vkCreatePipelineCache`'s own initial keys are subject to
/// the same bound, so a bounded cache handed more placeholder keys than it
/// can hold still respects its limit rather than starting over-full.
TEST(PipelineCacheBoundTest, InitialKeysRespectTheBound) {
  std::vector<PipelineCacheKey> InitialKeys;
  for (uint8_t I = 0; I != 8; ++I)
    InitialKeys.push_back(makeKey(I));

  PipelineCache Cache(InitialKeys, /*ExternallySynchronized=*/false,
                      /*MaxEntries=*/3);
  EXPECT_EQ(Cache.keys().size(), 3u);
}
