//===- PipelineTest.cpp - Shader module / pipeline tests -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "Pipeline.h"
#include "Descriptor.h"
#include "EntryPoints.h"
#include "Icd.h"
#include "Objects.h"

#include "feme/Target/CPU/CompiledStage.h"
#include "feme/Target/CPU/WaveSize.h"

#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Target/SPIRV/Serialization.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"

#include "gtest/gtest.h"

using namespace feme::vulkan;

namespace {

/// Parses \p Source (a single top-level `spirv.module`) and serializes it
/// to a raw binary word stream, the same input `vkCreateShaderModule`
/// expects (see "Input and specialization" in
/// feme/docs/FeMeVulkanDesign.md). Empty on parse/serialize failure.
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

/// A minimal `void main()` `GLCompute` entry point, with a `LocalSize`
/// execution mode of `1, 1, 1` -- "compile and execute a resource-free
/// SPIR-V compute shader using builtins" (V1's own scope).
const char *kEmptyComputeShader = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @main() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main
  spirv.ExecutionMode @main "LocalSize", 1, 1, 1
}
)mlir";

/// The same shader, but with no execution mode at all: group-size
/// resolution must fail cleanly rather than guess.
const char *kMissingExecutionModeShader = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @main() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main
}
)mlir";

/// Roadmap E4 (`VK_KHR_maintenance4`): the same minimal entry point, but
/// using SPIR-V 1.2's `LocalSizeId` execution mode -- three specialization
/// constants, not a plain `LocalSize` literal -- instead of `LocalSize`.
/// `maintenance4` adds no new opcode of its own here; it is the group-size
/// *resolution* path (`GroupSize.cpp`'s `resolveComputeGroupSize`) that
/// must accept this alternative spelling with no `LocalSize` present at
/// all, per `VK_KHR_maintenance4`'s own description ("Add support for the
/// SPIR-V 1.2 LocalSizeId execution mode").
const char *kLocalSizeIdComputeShader = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.2, [Shader], []> {
  spirv.SpecConstant @wg_x = 4 : i32
  spirv.SpecConstant @wg_y = 1 : i32
  spirv.SpecConstant @wg_z = 1 : i32
  spirv.func @main() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main
  spirv.ExecutionModeId @main "LocalSizeId" @wg_x, @wg_y, @wg_z
}
)mlir";

/// A `void main()` that reads and increments a `StorageBuffer` block bound
/// at (descriptor set 0, binding 0) -- V2's own "run a Vulkan compute
/// shader that reads and writes storage buffers" scenario, using a flat
/// (non-aggregate) `i32` element so `feme::cpu::SPIRVResourceLoweringPass`
/// normalizes the access (see that pass's header comment).
const char *kStorageBufferShader = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @buf bind(0, 0) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
  spirv.func @main() -> () "None" {
    %0 = spirv.mlir.addressof @buf : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0, %c0] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>, i32, i32 -> !spirv.ptr<i32, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : i32
    %c1 = spirv.Constant 1 : i32
    %v2 = spirv.IAdd %v, %c1 : i32
    spirv.Store "StorageBuffer" %ac, %v2 : i32
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main, @buf
  spirv.ExecutionMode @main "LocalSize", 1, 1, 1
}
)mlir";

/// V3: a `void main()` that reads a `PushConstant` block's sole `i32`
/// member and stores it into a `StorageBuffer` block -- exercises
/// `feme::cpu::SPIRVPushConstantLoweringPass` end to end through a real
/// compiled pipeline.
const char *kPushConstantShader = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @buf bind(0, 0) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
  spirv.GlobalVariable @pc : !spirv.ptr<!spirv.struct<(i32 [0])>, PushConstant>
  spirv.func @main() -> () "None" {
    %0 = spirv.mlir.addressof @buf : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0, %c0] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>, i32, i32 -> !spirv.ptr<i32, StorageBuffer>
    %1 = spirv.mlir.addressof @pc : !spirv.ptr<!spirv.struct<(i32 [0])>, PushConstant>
    %pcac = spirv.AccessChain %1[%c0] : !spirv.ptr<!spirv.struct<(i32 [0])>, PushConstant>, i32 -> !spirv.ptr<i32, PushConstant>
    %v = spirv.Load "PushConstant" %pcac : i32
    spirv.Store "StorageBuffer" %ac, %v : i32
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main, @buf, @pc
  spirv.ExecutionMode @main "LocalSize", 1, 1, 1
}
)mlir";

/// A `void main()` `GLCompute` entry point whose `LocalSize` execution mode
/// is a multiple of `feme::cpu::MinWaveSize` (4) in the X dimension --
/// roadmap E7's `VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT`
/// test fixtures use this shape (a multiple) and the one below (not a
/// multiple) to exercise both sides of that flag's validation.
const char *kLocalSizeXEightComputeShader = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @main() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main
  spirv.ExecutionMode @main "LocalSize", 8, 1, 1
}
)mlir";

/// The same shape, but with a local size X that is *not* a multiple of
/// `feme::cpu::MinWaveSize` (4).
const char *kLocalSizeXFiveComputeShader = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @main() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main
  spirv.ExecutionMode @main "LocalSize", 5, 1, 1
}
)mlir";

class PipelineTest : public ::testing::Test {
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
  }
  void TearDown() override {
    vkDestroyPipelineLayout(Device, Layout, nullptr);
    vkDestroyDevice(Device, nullptr);
    vkDestroyInstance(Instance, nullptr);
  }

  VkShaderModule createShaderModule(llvm::StringRef Source) {
    std::vector<uint32_t> Words = assembleSPIRV(Source);
    if (Words.empty())
      return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo CreateInfo{};
    CreateInfo.codeSize = Words.size() * sizeof(uint32_t);
    CreateInfo.pCode = Words.data();
    VkShaderModule Module = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateShaderModule(Device, &CreateInfo, nullptr, &Module),
              VK_SUCCESS);
    return Module;
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
  VkPipelineLayout Layout = VK_NULL_HANDLE;
};

TEST_F(PipelineTest, CompilesEmptyComputeShader) {
  VkShaderModule Module = createShaderModule(kEmptyComputeShader);
  ASSERT_NE(Module, VK_NULL_HANDLE);

  VkComputePipelineCreateInfo CreateInfo{};
  CreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  CreateInfo.stage.module = Module;
  CreateInfo.stage.pName = "main";
  CreateInfo.layout = Layout;

  VkPipeline Pipeline = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Pipeline),
            VK_SUCCESS);
  EXPECT_NE(Pipeline, VK_NULL_HANDLE);

  vkDestroyPipeline(Device, Pipeline, nullptr);
  vkDestroyShaderModule(Device, Module, nullptr);
}

/// Roadmap E4: an entry point declaring only `LocalSizeId` (no `LocalSize`
/// at all) compiles end to end -- `resolveComputeGroupSize` (GroupSize.cpp)
/// already resolves it from its three specialization constants' default
/// values, and `compileComputePipeline` (Pipeline.cpp) stamps the result
/// onto the compiled entry point exactly like a plain `LocalSize` shader.
TEST_F(PipelineTest, CompilesLocalSizeIdComputeShader) {
  VkShaderModule Module = createShaderModule(kLocalSizeIdComputeShader);
  ASSERT_NE(Module, VK_NULL_HANDLE);

  VkComputePipelineCreateInfo CreateInfo{};
  CreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  CreateInfo.stage.module = Module;
  CreateInfo.stage.pName = "main";
  CreateInfo.layout = Layout;

  VkPipeline Pipeline = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Pipeline),
            VK_SUCCESS);
  EXPECT_NE(Pipeline, VK_NULL_HANDLE);

  vkDestroyPipeline(Device, Pipeline, nullptr);
  vkDestroyShaderModule(Device, Module, nullptr);
}

TEST_F(PipelineTest, RejectsMissingGroupSizeInformation) {
  VkShaderModule Module = createShaderModule(kMissingExecutionModeShader);
  ASSERT_NE(Module, VK_NULL_HANDLE);

  VkComputePipelineCreateInfo CreateInfo{};
  CreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  CreateInfo.stage.module = Module;
  CreateInfo.stage.pName = "main";
  CreateInfo.layout = Layout;

  VkPipeline Pipeline = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Pipeline),
            VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Pipeline, VK_NULL_HANDLE);

  vkDestroyShaderModule(Device, Module, nullptr);
}

/// Roadmap E7 (`VK_EXT_subgroup_size_control`): a
/// `VkPipelineShaderStageRequiredSubgroupSizeCreateInfo` chained onto the
/// compute stage forces `compileComputePipeline` to compile at that exact
/// subgroup size instead of the host-derived default.
TEST_F(PipelineTest, HonorsRequiredSubgroupSizeOverride) {
  VkShaderModule Module = createShaderModule(kEmptyComputeShader);
  ASSERT_NE(Module, VK_NULL_HANDLE);

  VkPipelineShaderStageRequiredSubgroupSizeCreateInfo RequiredSize{};
  RequiredSize.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO;
  RequiredSize.requiredSubgroupSize = feme::cpu::MinWaveSize;

  VkComputePipelineCreateInfo CreateInfo{};
  CreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  CreateInfo.stage.pNext = &RequiredSize;
  CreateInfo.stage.module = Module;
  CreateInfo.stage.pName = "main";
  CreateInfo.layout = Layout;

  VkPipeline Pipeline = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Pipeline),
            VK_SUCCESS);
  EXPECT_EQ(fromHandle<ComputePipeline>(Pipeline)->getStage().getWaveSize(),
            feme::cpu::MinWaveSize);

  vkDestroyPipeline(Device, Pipeline, nullptr);
  vkDestroyShaderModule(Device, Module, nullptr);
}

/// A `requiredSubgroupSize` that is not a power of two in
/// `[MinSubgroupSize, MaxSubgroupSize]` must fail pipeline creation rather
/// than silently clamp -- the same validation
/// `feme::cpu::resolveWaveSize` already applies to `--wave-size`.
TEST_F(PipelineTest, RejectsInvalidRequiredSubgroupSize) {
  VkShaderModule Module = createShaderModule(kEmptyComputeShader);
  ASSERT_NE(Module, VK_NULL_HANDLE);

  VkPipelineShaderStageRequiredSubgroupSizeCreateInfo RequiredSize{};
  RequiredSize.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO;
  RequiredSize.requiredSubgroupSize = 3; // Not a power of two.

  VkComputePipelineCreateInfo CreateInfo{};
  CreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  CreateInfo.stage.pNext = &RequiredSize;
  CreateInfo.stage.module = Module;
  CreateInfo.stage.pName = "main";
  CreateInfo.layout = Layout;

  VkPipeline Pipeline = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Pipeline),
            VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Pipeline, VK_NULL_HANDLE);

  vkDestroyShaderModule(Device, Module, nullptr);
}

/// (roadmap F10) `VkPipelineRobustnessCreateInfo` chained onto either the
/// pipeline or its single stage is accepted, with the stage's own chain
/// taking precedence over the pipeline's per the extension's own spec
/// text -- neither changes this ICD's actual (already fully robust)
/// behavior, but both must be structurally accepted and validated rather
/// than rejected outright.
TEST_F(PipelineTest, AcceptsPipelineRobustnessCreateInfo) {
  VkShaderModule Module = createShaderModule(kEmptyComputeShader);
  ASSERT_NE(Module, VK_NULL_HANDLE);

  VkPipelineRobustnessCreateInfo StageRobustness{};
  StageRobustness.sType = VK_STRUCTURE_TYPE_PIPELINE_ROBUSTNESS_CREATE_INFO;
  StageRobustness.storageBuffers =
      VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED;
  StageRobustness.uniformBuffers =
      VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2;
  StageRobustness.vertexInputs =
      VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DEVICE_DEFAULT;
  StageRobustness.images = VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DISABLED;

  VkPipelineRobustnessCreateInfo PipelineRobustnessInfo{};
  PipelineRobustnessInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_ROBUSTNESS_CREATE_INFO;
  PipelineRobustnessInfo.storageBuffers =
      VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS;

  VkComputePipelineCreateInfo CreateInfo{};
  CreateInfo.pNext = &PipelineRobustnessInfo;
  CreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  CreateInfo.stage.pNext = &StageRobustness;
  CreateInfo.stage.module = Module;
  CreateInfo.stage.pName = "main";
  CreateInfo.layout = Layout;

  VkPipeline Pipeline = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Pipeline),
            VK_SUCCESS);
  const PipelineRobustness &Robustness =
      fromHandle<ComputePipeline>(Pipeline)->robustness();
  // The stage's own chain takes precedence over the pipeline-level one.
  EXPECT_EQ(Robustness.StorageBuffers,
            VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED);
  EXPECT_EQ(Robustness.UniformBuffers,
            VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2);
  EXPECT_EQ(Robustness.VertexInputs,
            VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DEVICE_DEFAULT);
  EXPECT_EQ(Robustness.Images, VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DISABLED);

  vkDestroyPipeline(Device, Pipeline, nullptr);
  vkDestroyShaderModule(Device, Module, nullptr);
}

/// An out-of-range `VkPipelineRobustnessBufferBehavior`/
/// `VkPipelineRobustnessImageBehavior` value must fail pipeline creation
/// rather than silently accept or misinterpret it.
TEST_F(PipelineTest, RejectsInvalidPipelineRobustnessBehavior) {
  VkShaderModule Module = createShaderModule(kEmptyComputeShader);
  ASSERT_NE(Module, VK_NULL_HANDLE);

  VkPipelineRobustnessCreateInfo Robustness{};
  Robustness.sType = VK_STRUCTURE_TYPE_PIPELINE_ROBUSTNESS_CREATE_INFO;
  Robustness.storageBuffers =
      static_cast<VkPipelineRobustnessBufferBehavior>(0xFFFF);

  VkComputePipelineCreateInfo CreateInfo{};
  CreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  CreateInfo.stage.pNext = &Robustness;
  CreateInfo.stage.module = Module;
  CreateInfo.stage.pName = "main";
  CreateInfo.layout = Layout;

  VkPipeline Pipeline = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Pipeline),
            VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Pipeline, VK_NULL_HANDLE);

  vkDestroyShaderModule(Device, Module, nullptr);
}
/// every subgroup launched is fully populated; this CPU target can only
/// honor that if the workgroup's X dimension is itself a multiple of the
/// resolved subgroup size, so pipeline creation must reject a shader whose
/// local size X isn't.
TEST_F(PipelineTest,
       RequireFullSubgroupsRejectsGroupSizeNotAMultipleOfSubgroupSize) {
  VkShaderModule Module = createShaderModule(kLocalSizeXFiveComputeShader);
  ASSERT_NE(Module, VK_NULL_HANDLE);

  VkPipelineShaderStageRequiredSubgroupSizeCreateInfo RequiredSize{};
  RequiredSize.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO;
  RequiredSize.requiredSubgroupSize = feme::cpu::MinWaveSize;

  VkComputePipelineCreateInfo CreateInfo{};
  CreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  CreateInfo.stage.flags =
      VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT;
  CreateInfo.stage.pNext = &RequiredSize;
  CreateInfo.stage.module = Module;
  CreateInfo.stage.pName = "main";
  CreateInfo.layout = Layout;

  VkPipeline Pipeline = VK_NULL_HANDLE;
  // 5 is not a multiple of the required subgroup size (4).
  EXPECT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Pipeline),
            VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Pipeline, VK_NULL_HANDLE);

  vkDestroyShaderModule(Device, Module, nullptr);
}

/// The converse of the rejection test above: a local size X that *is* a
/// multiple of the resolved subgroup size compiles successfully with
/// `VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT` set.
TEST_F(PipelineTest,
       RequireFullSubgroupsAcceptsGroupSizeThatIsAMultipleOfSubgroupSize) {
  VkShaderModule Module = createShaderModule(kLocalSizeXEightComputeShader);
  ASSERT_NE(Module, VK_NULL_HANDLE);

  VkPipelineShaderStageRequiredSubgroupSizeCreateInfo RequiredSize{};
  RequiredSize.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO;
  RequiredSize.requiredSubgroupSize = feme::cpu::MinWaveSize;

  VkComputePipelineCreateInfo CreateInfo{};
  CreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  CreateInfo.stage.flags =
      VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT;
  CreateInfo.stage.pNext = &RequiredSize;
  CreateInfo.stage.module = Module;
  CreateInfo.stage.pName = "main";
  CreateInfo.layout = Layout;

  VkPipeline Pipeline = VK_NULL_HANDLE;
  // 8 is a multiple of the required subgroup size (4).
  EXPECT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Pipeline),
            VK_SUCCESS);
  EXPECT_NE(Pipeline, VK_NULL_HANDLE);

  vkDestroyPipeline(Device, Pipeline, nullptr);
  vkDestroyShaderModule(Device, Module, nullptr);
}

TEST_F(PipelineTest, PipelineLayoutAcceptsDescriptorSetLayouts) {
  VkDescriptorSetLayoutBinding Binding{};
  Binding.binding = 0;
  Binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Binding.descriptorCount = 1;
  VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
  SetLayoutInfo.bindingCount = 1;
  SetLayoutInfo.pBindings = &Binding;
  VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreateDescriptorSetLayout(Device, &SetLayoutInfo, nullptr, &SetLayout),
      VK_SUCCESS);

  VkPipelineLayoutCreateInfo LayoutInfo{};
  LayoutInfo.setLayoutCount = 1;
  LayoutInfo.pSetLayouts = &SetLayout;
  VkPipelineLayout Accepted = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &Accepted),
            VK_SUCCESS);
  EXPECT_NE(Accepted, VK_NULL_HANDLE);

  vkDestroyPipelineLayout(Device, Accepted, nullptr);
  vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
}

TEST_F(PipelineTest, PipelineLayoutAcceptsPushConstantRanges) {
  // V3: push-constant ranges are accepted and recorded (see
  // "CompileRejectsRootConstantAccessNotCoveredByLayout"/
  // "CompilesRootConstantShaderWithCoveringLayout" below for the coverage
  // check this enables at pipeline-creation time).
  VkPushConstantRange Range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
  VkPipelineLayoutCreateInfo LayoutInfo{};
  LayoutInfo.pushConstantRangeCount = 1;
  LayoutInfo.pPushConstantRanges = &Range;

  VkPipelineLayout Layout = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &Layout),
            VK_SUCCESS);
  vkDestroyPipelineLayout(Device, Layout, nullptr);
}

TEST_F(PipelineTest, CompilesStorageBufferShaderWithCompatibleLayout) {
  VkDescriptorSetLayoutBinding Binding{};
  Binding.binding = 0;
  Binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Binding.descriptorCount = 1;
  VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
  SetLayoutInfo.bindingCount = 1;
  SetLayoutInfo.pBindings = &Binding;
  VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreateDescriptorSetLayout(Device, &SetLayoutInfo, nullptr, &SetLayout),
      VK_SUCCESS);
  VkPipelineLayoutCreateInfo LayoutInfo{};
  LayoutInfo.setLayoutCount = 1;
  LayoutInfo.pSetLayouts = &SetLayout;
  VkPipelineLayout StorageLayout = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &StorageLayout),
      VK_SUCCESS);

  VkShaderModule Module = createShaderModule(kStorageBufferShader);
  ASSERT_NE(Module, VK_NULL_HANDLE);

  VkComputePipelineCreateInfo CreateInfo{};
  CreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  CreateInfo.stage.module = Module;
  CreateInfo.stage.pName = "main";
  CreateInfo.layout = StorageLayout;

  VkPipeline Pipeline = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Pipeline),
            VK_SUCCESS);
  EXPECT_NE(Pipeline, VK_NULL_HANDLE);

  vkDestroyPipeline(Device, Pipeline, nullptr);
  vkDestroyShaderModule(Device, Module, nullptr);
  vkDestroyPipelineLayout(Device, StorageLayout, nullptr);
  vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
}

TEST_F(PipelineTest, RejectsStorageBufferShaderWithoutMatchingBinding) {
  // `Layout` (from SetUp) declares no descriptor sets at all, so the
  // shader's (set 0, binding 0) requirement cannot be satisfied.
  VkShaderModule Module = createShaderModule(kStorageBufferShader);
  ASSERT_NE(Module, VK_NULL_HANDLE);

  VkComputePipelineCreateInfo CreateInfo{};
  CreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  CreateInfo.stage.module = Module;
  CreateInfo.stage.pName = "main";
  CreateInfo.layout = Layout;

  VkPipeline Pipeline = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Pipeline),
            VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Pipeline, VK_NULL_HANDLE);

  vkDestroyShaderModule(Device, Module, nullptr);
}

namespace {
/// Builds a layout with one storage-buffer binding at (set 0, binding 0)
/// (`kPushConstantShader`'s own requirement), plus \p PushConstantRanges.
/// The caller owns and must destroy both the returned layout and
/// `*SetLayout`.
VkPipelineLayout createPushConstantShaderLayout(
    VkDevice Device, llvm::ArrayRef<VkPushConstantRange> PushConstantRanges,
    VkDescriptorSetLayout &SetLayout) {
  VkDescriptorSetLayoutBinding Binding{};
  Binding.binding = 0;
  Binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Binding.descriptorCount = 1;
  VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
  SetLayoutInfo.bindingCount = 1;
  SetLayoutInfo.pBindings = &Binding;
  if (vkCreateDescriptorSetLayout(Device, &SetLayoutInfo, nullptr,
                                  &SetLayout) != VK_SUCCESS)
    return VK_NULL_HANDLE;

  VkPipelineLayoutCreateInfo LayoutInfo{};
  LayoutInfo.setLayoutCount = 1;
  LayoutInfo.pSetLayouts = &SetLayout;
  LayoutInfo.pushConstantRangeCount = PushConstantRanges.size();
  LayoutInfo.pPushConstantRanges = PushConstantRanges.data();
  VkPipelineLayout Layout = VK_NULL_HANDLE;
  vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &Layout);
  return Layout;
}
} // namespace

TEST_F(PipelineTest, RejectsRootConstantAccessNotCoveredByLayout) {
  // No push-constant range at all: the shader's root-constant span (4
  // bytes) is not covered by anything, so pipeline creation must fail (see
  // "Descriptor Model": "reject a shader whose accessed range is not
  // fully covered").
  VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
  VkPipelineLayout UncoveredLayout =
      createPushConstantShaderLayout(Device, {}, SetLayout);
  ASSERT_NE(UncoveredLayout, VK_NULL_HANDLE);

  VkShaderModule Module = createShaderModule(kPushConstantShader);
  ASSERT_NE(Module, VK_NULL_HANDLE);

  VkComputePipelineCreateInfo CreateInfo{};
  CreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  CreateInfo.stage.module = Module;
  CreateInfo.stage.pName = "main";
  CreateInfo.layout = UncoveredLayout;

  VkPipeline Pipeline = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Pipeline),
            VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Pipeline, VK_NULL_HANDLE);

  vkDestroyShaderModule(Device, Module, nullptr);
  vkDestroyPipelineLayout(Device, UncoveredLayout, nullptr);
  vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
}

TEST_F(PipelineTest, CompilesRootConstantShaderWithCoveringLayout) {
  VkPushConstantRange Range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
  VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
  VkPipelineLayout CoveredLayout =
      createPushConstantShaderLayout(Device, Range, SetLayout);
  ASSERT_NE(CoveredLayout, VK_NULL_HANDLE);

  VkShaderModule Module = createShaderModule(kPushConstantShader);
  ASSERT_NE(Module, VK_NULL_HANDLE);

  VkComputePipelineCreateInfo CreateInfo{};
  CreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  CreateInfo.stage.module = Module;
  CreateInfo.stage.pName = "main";
  CreateInfo.layout = CoveredLayout;

  VkPipeline Pipeline = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Pipeline),
            VK_SUCCESS);
  EXPECT_NE(Pipeline, VK_NULL_HANDLE);

  vkDestroyPipeline(Device, Pipeline, nullptr);
  vkDestroyShaderModule(Device, Module, nullptr);
  vkDestroyPipelineLayout(Device, CoveredLayout, nullptr);
  vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
}

TEST_F(PipelineTest, RejectsNonComputeVisiblePushConstantRange) {
  // A range that exists but does not carry the compute stage bit does not
  // count as coverage (see "Descriptor Model": "...declared in the
  // layout with the compute stage bit set").
  VkPushConstantRange Range{VK_SHADER_STAGE_VERTEX_BIT, 0, 4};
  VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
  VkPipelineLayout NonComputeLayout =
      createPushConstantShaderLayout(Device, Range, SetLayout);
  ASSERT_NE(NonComputeLayout, VK_NULL_HANDLE);

  VkShaderModule Module = createShaderModule(kPushConstantShader);
  ASSERT_NE(Module, VK_NULL_HANDLE);

  VkComputePipelineCreateInfo CreateInfo{};
  CreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  CreateInfo.stage.module = Module;
  CreateInfo.stage.pName = "main";
  CreateInfo.layout = NonComputeLayout;

  VkPipeline Pipeline = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Pipeline),
            VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Pipeline, VK_NULL_HANDLE);

  vkDestroyShaderModule(Device, Module, nullptr);
  vkDestroyPipelineLayout(Device, NonComputeLayout, nullptr);
  vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
}

// Roadmap H6u: `pushConstantsCoverRootConstantSize`'s own coverage check
// must be scoped to `[RootConstantMinOffset, RootConstantSize)`, not
// unconditionally `[0, RootConstantSize)` -- a real, legal SPIR-V shape
// (multiple stages sharing one push-constant block, each declaring a
// nonzero leading `layout(offset=N)`) genuinely never accesses the bytes
// below its own `RootConstantMinOffset`, so a `VkPushConstantRange` that
// only covers its own accessed span (and not the whole struct) must still
// be accepted. These call the function directly (rather than through a
// full pipeline-creation round trip) since constructing a real SPIR-V
// shader with a nonzero push-constant `layout(offset=N)` needs more
// machinery than this unit warrants -- the end-to-end shape is already
// covered by the two tests above for the always-zero-offset case, and by
// `dEQP-VK.mesh_shader.ext.api.draw.*with_task_shader*` in the real
// Vulkan CTS for the nonzero case this fixes.
TEST_F(PipelineTest, PushConstantsCoverRootConstantSizeHonorsMinOffset) {
  PipelineLayout EmptyLayout({}, {});

  // A layout with no ranges at all still trivially "covers" an empty span
  // once `RootConstantMinOffset == RootConstantSize` (nothing is ever
  // accessed): not a real shape any lowering pass produces, but a
  // defensive edge the byte-walk must not stumble on.
  EXPECT_TRUE(pushConstantsCoverRootConstantSize(
      EmptyLayout, /*RootConstantSize=*/20, /*RootConstantMinOffset=*/20,
      /*MaxPushConstantsSize=*/128, VK_SHADER_STAGE_TASK_BIT_EXT));

  // A range covering only `[12, 20)` (the task stage's own accessed
  // portion of a shared 20-byte push-constant block) does not cover
  // `[0, 20)` -- but does cover `[12, 20)`, the actual reflected span once
  // `RootConstantMinOffset == 12` is honored instead of assuming 0.
  VkPushConstantRange TaskRange{VK_SHADER_STAGE_TASK_BIT_EXT, 12, 8};
  PipelineLayout TaskOnlyLayout({}, {TaskRange});
  EXPECT_FALSE(pushConstantsCoverRootConstantSize(
      TaskOnlyLayout, /*RootConstantSize=*/20, /*RootConstantMinOffset=*/0,
      /*MaxPushConstantsSize=*/128, VK_SHADER_STAGE_TASK_BIT_EXT));
  EXPECT_TRUE(pushConstantsCoverRootConstantSize(
      TaskOnlyLayout, /*RootConstantSize=*/20, /*RootConstantMinOffset=*/12,
      /*MaxPushConstantsSize=*/128, VK_SHADER_STAGE_TASK_BIT_EXT));

  // A range that only partially covers `[RootConstantMinOffset,
  // RootConstantSize)` (missing its last byte) is still rejected -- the
  // fix narrows which bytes must be covered, it does not weaken the
  // byte-exact coverage check itself.
  VkPushConstantRange PartialTaskRange{VK_SHADER_STAGE_TASK_BIT_EXT, 12, 7};
  PipelineLayout PartialTaskLayout({}, {PartialTaskRange});
  EXPECT_FALSE(pushConstantsCoverRootConstantSize(
      PartialTaskLayout, /*RootConstantSize=*/20, /*RootConstantMinOffset=*/12,
      /*MaxPushConstantsSize=*/128, VK_SHADER_STAGE_TASK_BIT_EXT));
}

/// Roadmap E19 (`VK_EXT_pipeline_creation_feedback`): a chained
/// `VkPipelineCreationFeedbackCreateInfo` with one stage-feedback slot (a
/// compute pipeline has exactly one stage) gets a `VALID_BIT`-only overall
/// and per-stage feedback -- no `VkPipelineCache` is involved here, so
/// `PipelineCacheTest.CreationFeedbackReportsCacheHitOnSecondCreation`
/// covers the cache-hit flag instead.
TEST_F(PipelineTest, ReportsPipelineCreationFeedback) {
  VkShaderModule Module = createShaderModule(kEmptyComputeShader);
  ASSERT_NE(Module, VK_NULL_HANDLE);

  VkPipelineCreationFeedback Feedback{};
  Feedback.flags = 0xdeadbeef; // Must be overwritten, not merely OR'd into.
  VkPipelineCreationFeedback StageFeedback{};
  VkPipelineCreationFeedbackCreateInfo FeedbackInfo{};
  FeedbackInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO;
  FeedbackInfo.pPipelineCreationFeedback = &Feedback;
  FeedbackInfo.pipelineStageCreationFeedbackCount = 1;
  FeedbackInfo.pPipelineStageCreationFeedbacks = &StageFeedback;

  VkComputePipelineCreateInfo CreateInfo{};
  CreateInfo.pNext = &FeedbackInfo;
  CreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  CreateInfo.stage.module = Module;
  CreateInfo.stage.pName = "main";
  CreateInfo.layout = Layout;

  VkPipeline Pipeline = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Pipeline),
            VK_SUCCESS);

  EXPECT_EQ(Feedback.flags, static_cast<VkPipelineCreationFeedbackFlags>(
                                VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT));
  EXPECT_EQ(Feedback.duration, 0u);
  EXPECT_EQ(StageFeedback.flags, static_cast<VkPipelineCreationFeedbackFlags>(
                                     VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT));
  EXPECT_EQ(StageFeedback.duration, 0u);

  vkDestroyPipeline(Device, Pipeline, nullptr);
  vkDestroyShaderModule(Device, Module, nullptr);
}

/// Roadmap F9 (`VK_EXT_pipeline_protected_access`): both of the extension's
/// mutually-exclusive restriction bits are legal `VkComputePipelineCreateInfo
/// ::flags` on their own -- creation must succeed and record the flag
/// verbatim on the resulting `Pipeline` object (`Pipeline::createFlags`),
/// exactly the way `vkCmdBindPipeline` (CommandBufferTest.cpp's
/// `BindingAProtectedAccessOnlyPipelineIsSilentlyRejected`) consults it.
TEST_F(PipelineTest, AcceptsNoProtectedAccessCreateFlag) {
  VkShaderModule Module = createShaderModule(kEmptyComputeShader);
  ASSERT_NE(Module, VK_NULL_HANDLE);

  VkComputePipelineCreateInfo CreateInfo{};
  CreateInfo.flags = VK_PIPELINE_CREATE_NO_PROTECTED_ACCESS_BIT;
  CreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  CreateInfo.stage.module = Module;
  CreateInfo.stage.pName = "main";
  CreateInfo.layout = Layout;

  VkPipeline Pipeline = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Pipeline),
            VK_SUCCESS);
  EXPECT_EQ(fromHandle<feme::vulkan::Pipeline>(Pipeline)->createFlags() &
                VK_PIPELINE_CREATE_NO_PROTECTED_ACCESS_BIT,
            static_cast<VkPipelineCreateFlags>(
                VK_PIPELINE_CREATE_NO_PROTECTED_ACCESS_BIT));

  vkDestroyPipeline(Device, Pipeline, nullptr);
  vkDestroyShaderModule(Device, Module, nullptr);
}

/// Roadmap F9: the other restriction bit -- also legal on its own at
/// creation (only `vkCmdBindPipeline`'s own bind-time rule rejects it, per
/// `VUID-vkCmdBindPipeline-pipelineProtectedAccess-07409`, since this ICD
/// never hands out a protected command buffer to legally bind it in).
TEST_F(PipelineTest, AcceptsProtectedAccessOnlyCreateFlag) {
  VkShaderModule Module = createShaderModule(kEmptyComputeShader);
  ASSERT_NE(Module, VK_NULL_HANDLE);

  VkComputePipelineCreateInfo CreateInfo{};
  CreateInfo.flags = VK_PIPELINE_CREATE_PROTECTED_ACCESS_ONLY_BIT;
  CreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  CreateInfo.stage.module = Module;
  CreateInfo.stage.pName = "main";
  CreateInfo.layout = Layout;

  VkPipeline Pipeline = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &CreateInfo,
                                     nullptr, &Pipeline),
            VK_SUCCESS);
  EXPECT_EQ(fromHandle<feme::vulkan::Pipeline>(Pipeline)->createFlags() &
                VK_PIPELINE_CREATE_PROTECTED_ACCESS_ONLY_BIT,
            static_cast<VkPipelineCreateFlags>(
                VK_PIPELINE_CREATE_PROTECTED_ACCESS_ONLY_BIT));

  vkDestroyPipeline(Device, Pipeline, nullptr);
  vkDestroyShaderModule(Device, Module, nullptr);
}

TEST(ShaderModuleTest, RejectsMisalignedCodeSize) {
  VkShaderModuleCreateInfo CreateInfo{};
  uint32_t Code[1] = {0};
  CreateInfo.codeSize = 3; // Not a multiple of 4.
  CreateInfo.pCode = Code;
  VkShaderModule Module = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateShaderModule(VK_NULL_HANDLE, &CreateInfo, nullptr, &Module),
            VK_ERROR_INITIALIZATION_FAILED);
}

/// (roadmap L12c) `patchUnboundedResourceRanges` is exercised directly on
/// raw LLVM IR, mirroring `SPIRVResourceLoweringTest.cpp`'s own
/// `LeavesUnboundedArrayUnchanged` case that this milestone's own gap
/// analysis started from: same `handlefrombinding` shape, `RangeSize`
/// operand 0 (the unbounded/`RuntimeDescriptorArray` sentinel -- see
/// `getArrayedResourceCount` in SPIRVToLLVMPatterns.cpp), (set, binding) =
/// (0, 1). A `PipelineLayout` whose one descriptor set declares that
/// binding with `Count = 3` (mirroring `overflow-unbounded-array.test`'s own
/// `ArraySize: 3`) should see that operand rewritten to the constant `3`,
/// leaving every other operand (including the unrelated (set 0, binding 0)
/// call, whose own `RangeSize` is already the non-zero constant `1`, i.e.
/// bounded) untouched.
TEST(PatchUnboundedResourceRangesTest, RewritesUnboundedRangeToLayoutCount) {
  llvm::LLVMContext Ctx;
  llvm::SMDiagnostic Err;
  std::unique_ptr<llvm::Module> M = llvm::parseAssemblyString(R"(
    define void @main(i32 %idx) {
      %h0 = call target("spirv.VulkanBuffer", [0 x i32], 12, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0,
                                               ptr null)
      %h1 = call target("spirv.VulkanBuffer", [0 x i32], 12, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 0, i32 %idx,
                                               ptr null)
      ret void
    }
    declare target("spirv.VulkanBuffer", [0 x i32], 12, 0)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
  )",
                                                              Err, Ctx);
  ASSERT_TRUE(M) << Err.getMessage().str();

  DescriptorSetLayout SetLayout({
      DescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
      DescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
  });
  PipelineLayout Layout({&SetLayout}, {});

  patchUnboundedResourceRanges(*M, Layout);

  llvm::Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  bool SawBoundedCall = false, SawUnboundedCall = false;
  for (llvm::Instruction &I : llvm::instructions(F)) {
    auto *CI = llvm::dyn_cast<llvm::CallInst>(&I);
    if (!CI || !CI->getCalledFunction() ||
        CI->getCalledFunction()->getIntrinsicID() !=
            llvm::Intrinsic::spv_resource_handlefrombinding)
      continue;
    auto *BindingC = llvm::cast<llvm::ConstantInt>(CI->getArgOperand(1));
    auto *RangeSizeC = llvm::cast<llvm::ConstantInt>(CI->getArgOperand(2));
    if (BindingC->getZExtValue() == 0) {
      SawBoundedCall = true;
      EXPECT_EQ(RangeSizeC->getZExtValue(), 1u); // Unchanged.
    } else {
      SawUnboundedCall = true;
      EXPECT_EQ(RangeSizeC->getZExtValue(), 3u); // Patched to Count.
    }
  }
  EXPECT_TRUE(SawBoundedCall);
  EXPECT_TRUE(SawUnboundedCall);
}

/// A `handlefrombinding` whose (set, binding) is not declared by the
/// `PipelineLayout` at all is left with `RangeSize` unpatched (still `0`) --
/// `validateBoundRanges` (Pipeline.cpp) reports that as a real "shader's
/// (set, binding) requirement is not satisfied" pipeline-creation error
/// later, which is more informative than this rewrite silently guessing a
/// count.
TEST(PatchUnboundedResourceRangesTest, LeavesUndeclaredBindingUnpatched) {
  llvm::LLVMContext Ctx;
  llvm::SMDiagnostic Err;
  std::unique_ptr<llvm::Module> M = llvm::parseAssemblyString(R"(
    define void @main(i32 %idx) {
      %h = call target("spirv.VulkanBuffer", [0 x i32], 12, 0)
          @llvm.spv.resource.handlefrombinding(i32 0, i32 9, i32 0, i32 %idx,
                                               ptr null)
      ret void
    }
    declare target("spirv.VulkanBuffer", [0 x i32], 12, 0)
        @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
  )",
                                                              Err, Ctx);
  ASSERT_TRUE(M) << Err.getMessage().str();

  DescriptorSetLayout SetLayout(
      {DescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}});
  PipelineLayout Layout({&SetLayout}, {});

  patchUnboundedResourceRanges(*M, Layout);

  llvm::Function *F = M->getFunction("main");
  ASSERT_TRUE(F);
  for (llvm::Instruction &I : llvm::instructions(F)) {
    auto *CI = llvm::dyn_cast<llvm::CallInst>(&I);
    if (!CI || !CI->getCalledFunction() ||
        CI->getCalledFunction()->getIntrinsicID() !=
            llvm::Intrinsic::spv_resource_handlefrombinding)
      continue;
    EXPECT_EQ(llvm::cast<llvm::ConstantInt>(CI->getArgOperand(2))
                  ->getZExtValue(),
              0u);
  }
}

} // namespace
