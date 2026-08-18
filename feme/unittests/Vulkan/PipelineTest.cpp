//===- PipelineTest.cpp - Shader module / pipeline tests -----------------===//
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

class PipelineTest : public ::testing::Test {
protected:
  void SetUp() override {
    VkInstanceCreateInfo InstInfo{};
    ASSERT_EQ(vkCreateInstance(&InstInfo, nullptr, &Instance), VK_SUCCESS);
    uint32_t Count = 1;
    ASSERT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Physical),
              VK_SUCCESS);
    VkDeviceCreateInfo DevInfo{};
    ASSERT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Device),
              VK_SUCCESS);

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

TEST_F(PipelineTest, PipelineLayoutRejectsDescriptorSets) {
  VkDescriptorSetLayout FakeSetLayout =
      reinterpret_cast<VkDescriptorSetLayout>(1);
  VkPipelineLayoutCreateInfo LayoutInfo{};
  LayoutInfo.setLayoutCount = 1;
  LayoutInfo.pSetLayouts = &FakeSetLayout;

  VkPipelineLayout Rejected = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &Rejected),
            VK_ERROR_INITIALIZATION_FAILED);
}

TEST(ShaderModuleTest, RejectsMisalignedCodeSize) {
  VkShaderModuleCreateInfo CreateInfo{};
  uint32_t Code[1] = {0};
  CreateInfo.codeSize = 3; // Not a multiple of 4.
  CreateInfo.pCode = Code;
  VkShaderModule Module = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateShaderModule(VK_NULL_HANDLE, &CreateInfo, nullptr,
                                 &Module),
            VK_ERROR_INITIALIZATION_FAILED);
}

} // namespace
