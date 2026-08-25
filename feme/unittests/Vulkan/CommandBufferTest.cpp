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
#include "Descriptor.h"
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

/// V4 ("broader subgroup ... coverage"): reads the `SubgroupSize` and
/// `SubgroupLocalInvocationId` builtins and writes both into a
/// `StorageBuffer` -- exercises the CPU target's lowering of
/// `llvm.spv.subgroup.size`/`llvm.spv.subgroup.local.invocation.id`
/// (feme::cpu::SIMDizePass's `classifyWaveCall`/`classifyBuiltin`) end to
/// end through a real dispatch, closing a gap where those two builtins
/// converted to intrinsic calls with no CPU-target lowering at all (see
/// "Builtin and execution-shape mapping" in feme/docs/FeMeVulkanDesign.md).
const char *kSubgroupBuiltinShader = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, GroupNonUniform], []> {
  spirv.GlobalVariable @size built_in("SubgroupSize") : !spirv.ptr<i32, Input>
  spirv.GlobalVariable @lane built_in("SubgroupLocalInvocationId") : !spirv.ptr<i32, Input>
  spirv.GlobalVariable @out bind(0, 0) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>
  spirv.func @main() -> () "None" {
    %0 = spirv.mlir.addressof @size : !spirv.ptr<i32, Input>
    %size = spirv.Load "Input" %0 : i32
    %1 = spirv.mlir.addressof @lane : !spirv.ptr<i32, Input>
    %lane = spirv.Load "Input" %1 : i32
    %2 = spirv.mlir.addressof @out : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %ac0 = spirv.AccessChain %2[%c0, %c0] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>, i32, i32 -> !spirv.ptr<i32, StorageBuffer>
    spirv.Store "StorageBuffer" %ac0, %size : i32
    %ac1 = spirv.AccessChain %2[%c0, %c1] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>, i32, i32 -> !spirv.ptr<i32, StorageBuffer>
    spirv.Store "StorageBuffer" %ac1, %lane : i32
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main, @size, @lane, @out
  spirv.ExecutionMode @main "LocalSize", 1, 1, 1
}
)mlir";

/// Reads `in[gid.x]`, adds one, and writes the result to `out[gid.x]` --
/// two flat (non-aggregate) `i32` `StorageBuffer` bindings in one
/// descriptor set, matching V2's own "run a Vulkan compute shader that
/// reads and writes storage buffers" scenario.
const char *kStorageBufferCopyShader = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @gid built_in("GlobalInvocationId") : !spirv.ptr<vector<3xi32>, Input>
  spirv.GlobalVariable @in bind(0, 0) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>
  spirv.GlobalVariable @out bind(0, 1) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>
  spirv.func @main() -> () "None" {
    %0 = spirv.mlir.addressof @gid : !spirv.ptr<vector<3xi32>, Input>
    %1 = spirv.Load "Input" %0 : vector<3xi32>
    %idx = spirv.CompositeExtract %1[0 : i32] : vector<3xi32>
    %2 = spirv.mlir.addressof @in : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>
    %c0 = spirv.Constant 0 : i32
    %ac_in = spirv.AccessChain %2[%c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>, i32, i32 -> !spirv.ptr<i32, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac_in : i32
    %c1 = spirv.Constant 1 : i32
    %v2 = spirv.IAdd %v, %c1 : i32
    %3 = spirv.mlir.addressof @out : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>
    %ac_out = spirv.AccessChain %3[%c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>, i32, i32 -> !spirv.ptr<i32, StorageBuffer>
    spirv.Store "StorageBuffer" %ac_out, %v2 : i32
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main, @gid, @in, @out
  spirv.ExecutionMode @main "LocalSize", 1, 1, 1
}
)mlir";

/// (Roadmap R30 / V5) Samples a bound 2D sampled image through a separate
/// bound sampler and writes the four resulting components to a
/// `StorageBuffer` -- the first shader in this ICD that *consumes* an
/// image, which V5 could only create and copy. An explicit LOD is used
/// rather than an implicit one because a compute shader has no fragment
/// derivatives to compute one from.
const char *kSampledImageShader = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.GlobalVariable @out bind(0, 2) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0])>, StorageBuffer>
  spirv.func @main() -> () "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %image = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %1 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %sampler = spirv.Load "UniformConstant" %1 : !spirv.sampler
    %si = spirv.SampledImage %image, %sampler : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %uv = spirv.Constant dense<[7.500000e-01, 7.500000e-01]> : vector<2xf32>
    %lod = spirv.Constant 0.000000e+00 : f32
    %texel = spirv.ImageSampleExplicitLod %si, %uv ["Lod"], %lod : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32 -> vector<4xf32>
    %2 = spirv.mlir.addressof @out : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0])>, StorageBuffer>
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %c2 = spirv.Constant 2 : i32
    %c3 = spirv.Constant 3 : i32
    %r = spirv.CompositeExtract %texel[0 : i32] : vector<4xf32>
    %g = spirv.CompositeExtract %texel[1 : i32] : vector<4xf32>
    %b = spirv.CompositeExtract %texel[2 : i32] : vector<4xf32>
    %a = spirv.CompositeExtract %texel[3 : i32] : vector<4xf32>
    %ac0 = spirv.AccessChain %2[%c0, %c0] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0])>, StorageBuffer>, i32, i32 -> !spirv.ptr<f32, StorageBuffer>
    spirv.Store "StorageBuffer" %ac0, %r : f32
    %ac1 = spirv.AccessChain %2[%c0, %c1] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0])>, StorageBuffer>, i32, i32 -> !spirv.ptr<f32, StorageBuffer>
    spirv.Store "StorageBuffer" %ac1, %g : f32
    %ac2 = spirv.AccessChain %2[%c0, %c2] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0])>, StorageBuffer>, i32, i32 -> !spirv.ptr<f32, StorageBuffer>
    spirv.Store "StorageBuffer" %ac2, %b : f32
    %ac3 = spirv.AccessChain %2[%c0, %c3] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0])>, StorageBuffer>, i32, i32 -> !spirv.ptr<f32, StorageBuffer>
    spirv.Store "StorageBuffer" %ac3, %a : f32
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main, @img, @samp, @out
  spirv.ExecutionMode @main "LocalSize", 1, 1, 1
}
)mlir";

/// V3: reads a single `StorageBuffer` element, adds a `PushConstant`
/// block's sole `i32` member, and writes the result back -- exercises the
/// "combined" push-constant + bound-resource lowering path end to end
/// through a real dispatch (see
/// `feme::cpu::SPIRVResourceLoweringPass`'s own header comment for that
/// combination).
const char *kPushConstantAddShader = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @buf bind(0, 0) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>
  spirv.GlobalVariable @pc : !spirv.ptr<!spirv.struct<(i32 [0])>, PushConstant>
  spirv.func @main() -> () "None" {
    %0 = spirv.mlir.addressof @buf : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0, %c0] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>, i32, i32 -> !spirv.ptr<i32, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : i32
    %1 = spirv.mlir.addressof @pc : !spirv.ptr<!spirv.struct<(i32 [0])>, PushConstant>
    %pcac = spirv.AccessChain %1[%c0] : !spirv.ptr<!spirv.struct<(i32 [0])>, PushConstant>, i32 -> !spirv.ptr<i32, PushConstant>
    %pcv = spirv.Load "PushConstant" %pcac : i32
    %v2 = spirv.IAdd %v, %pcv : i32
    spirv.Store "StorageBuffer" %ac, %v2 : i32
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main, @buf, @pc
  spirv.ExecutionMode @main "LocalSize", 1, 1, 1
}
)mlir";

/// V4: reads one texel (a compile-time-constant coordinate: this milestone
/// stops at "widening a divergent, vector-valued resource result" being a
/// pre-existing, documented `feme::cpu::SIMDizePass` gap -- see the
/// `Deviation` note in FeMeVulkanDesign.md's V4 status for the full
/// writeup) from a uniform texel buffer (`Buffer<float4>` in HLSL --
/// `OpImageFetch`, Sampled == 1), adds a constant, and writes the result to
/// a storage texel buffer (`RWBuffer<float4>` -- `OpImageWrite`,
/// Sampled == 2). Exercises both texel-buffer descriptor kinds' shader-side
/// lowering (`classifyTexelBufferHandle`'s `TexelUniform`/`TexelStorage`) in
/// one real dispatch.
const char *kTexelBufferAddShader = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @in bind(0, 0) : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NeedSampler, Rgba32f>, UniformConstant>
  spirv.GlobalVariable @out bind(0, 1) : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
  spirv.func @main() -> () "None" {
    %idx = spirv.Constant 0 : i32
    %2 = spirv.mlir.addressof @in : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NeedSampler, Rgba32f>, UniformConstant>
    %img_in = spirv.Load "UniformConstant" %2 : !spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NeedSampler, Rgba32f>
    %v = spirv.ImageFetch %img_in, %idx : !spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NeedSampler, Rgba32f>, i32 -> vector<4xf32>
    %one = spirv.Constant dense<1.0> : vector<4xf32>
    %v2 = spirv.FAdd %v, %one : vector<4xf32>
    %3 = spirv.mlir.addressof @out : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
    %img_out = spirv.Load "UniformConstant" %3 : !spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>
    spirv.ImageWrite %img_out, %idx, %v2 : !spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, i32, vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main, @in, @out
  spirv.ExecutionMode @main "LocalSize", 1, 1, 1
}
)mlir";

/// V4: the `<4 x i32>` (integer-format) counterpart of
/// `kTexelBufferAddShader` above -- reads one texel from a `Rgba32ui`
/// uniform texel buffer, adds a constant, and writes it to a `Rgba32i`
/// storage texel buffer, exercising `isSupportedTexelElementType`'s (V4)
/// `<4 x i32>` acceptance and `femeCpuResourceLoadTypedV4I32`/
/// `StoreTypedV4I32` end to end. The in/out images intentionally use
/// different (both 32-bit-identity) integer formats to prove the
/// conversion is keyed off each descriptor's own bound `Format`, not
/// baked into the shader.
const char *kIntTexelBufferAddShader = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @in bind(0, 0) : !spirv.ptr<!spirv.image<i32, Buffer, NoDepth, NonArrayed, SingleSampled, NeedSampler, Rgba32ui>, UniformConstant>
  spirv.GlobalVariable @out bind(0, 1) : !spirv.ptr<!spirv.image<i32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32i>, UniformConstant>
  spirv.func @main() -> () "None" {
    %idx = spirv.Constant 0 : i32
    %2 = spirv.mlir.addressof @in : !spirv.ptr<!spirv.image<i32, Buffer, NoDepth, NonArrayed, SingleSampled, NeedSampler, Rgba32ui>, UniformConstant>
    %img_in = spirv.Load "UniformConstant" %2 : !spirv.image<i32, Buffer, NoDepth, NonArrayed, SingleSampled, NeedSampler, Rgba32ui>
    %v = spirv.ImageFetch %img_in, %idx : !spirv.image<i32, Buffer, NoDepth, NonArrayed, SingleSampled, NeedSampler, Rgba32ui>, i32 -> vector<4xi32>
    %one = spirv.Constant dense<1> : vector<4xi32>
    %v2 = spirv.IAdd %v, %one : vector<4xi32>
    %3 = spirv.mlir.addressof @out : !spirv.ptr<!spirv.image<i32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32i>, UniformConstant>
    %img_out = spirv.Load "UniformConstant" %3 : !spirv.image<i32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32i>
    spirv.ImageWrite %img_out, %idx, %v2 : !spirv.image<i32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32i>, i32, vector<4xi32>
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main, @in, @out
  spirv.ExecutionMode @main "LocalSize", 1, 1, 1
}
)mlir";

/// V3: reads the second field of a `Uniform` storage-class block --
/// `cbuffer`/`ConstantBuffer<T>` in HLSL -- and writes it to a
/// `StorageBuffer` element, exercising the SPIR-V shader-side uniform-
/// buffer lowering (`feme::spirv::UniformBufferAccessChainPattern`,
/// `feme::cpu::SPIRVResourceLoweringPass`'s uniform-buffer handling) end to
/// end. Reading the *second* field (rather than the first) confirms the
/// field resolves to its own struct-layout byte offset, not just field 0's.
const char *kUniformBufferReadShader = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @cb bind(0, 0) : !spirv.ptr<!spirv.struct<(!spirv.struct<(i32 [0], i32 [4])> [0])>, Uniform>
  spirv.GlobalVariable @out bind(0, 1) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>
  spirv.func @main() -> () "None" {
    %0 = spirv.mlir.addressof @cb : !spirv.ptr<!spirv.struct<(!spirv.struct<(i32 [0], i32 [4])> [0])>, Uniform>
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %ac = spirv.AccessChain %0[%c0, %c1] : !spirv.ptr<!spirv.struct<(!spirv.struct<(i32 [0], i32 [4])> [0])>, Uniform>, i32, i32 -> !spirv.ptr<i32, Uniform>
    %v = spirv.Load "Uniform" %ac : i32
    %1 = spirv.mlir.addressof @out : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>
    %ac_out = spirv.AccessChain %1[%c0, %c0] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<i32, stride=4> [0])>, StorageBuffer>, i32, i32 -> !spirv.ptr<i32, StorageBuffer>
    spirv.Store "StorageBuffer" %ac_out, %v : i32
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main, @cb, @out
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

/// Roadmap F9 (`VK_EXT_pipeline_protected_access`): a pipeline created with
/// `VK_PIPELINE_CREATE_PROTECTED_ACCESS_ONLY_BIT` can never be legally bound
/// anywhere in this ICD (`VUID-vkCmdBindPipeline-pipelineProtectedAccess-
/// 07409`: no `VkCommandBuffer` it hands out is ever protected, since
/// `protectedMemory` always reports `VK_FALSE`), so `vkCmdBindPipeline`
/// silently skips recording the bind -- proven here the same way
/// `DispatchWithoutBoundPipelineFails` above proves no pipeline was ever
/// bound: only the dispatch itself is recorded, and executing it fails for
/// lack of a bound pipeline.
TEST_F(CommandBufferTest,
       BindingAProtectedAccessOnlyPipelineIsSilentlyRejected) {
  VkComputePipelineCreateInfo PipelineInfo{};
  PipelineInfo.flags = VK_PIPELINE_CREATE_PROTECTED_ACCESS_ONLY_BIT;
  PipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  PipelineInfo.stage.module = Module;
  PipelineInfo.stage.pName = "main";
  PipelineInfo.layout = Layout;
  VkPipeline ProtectedOnlyPipeline = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &PipelineInfo,
                                     nullptr, &ProtectedOnlyPipeline),
            VK_SUCCESS);

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(CmdBuf, &BeginInfo), VK_SUCCESS);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE,
                    ProtectedOnlyPipeline);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  ASSERT_EQ(vkEndCommandBuffer(CmdBuf), VK_SUCCESS);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  EXPECT_EQ(Recorded->commands().size(), 1u); // The bind was never recorded.
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Failed());

  vkDestroyPipeline(Device, ProtectedOnlyPipeline, nullptr);
}

/// Roadmap F9: the other restriction bit, `VK_PIPELINE_CREATE_NO_PROTECTED_
/// ACCESS_BIT`, trivially satisfies its own bind-time rule instead (every
/// command buffer this ICD creates is already unprotected), so it binds and
/// dispatches exactly like an unflagged pipeline.
TEST_F(CommandBufferTest, BindingANoProtectedAccessPipelineSucceeds) {
  VkComputePipelineCreateInfo PipelineInfo{};
  PipelineInfo.flags = VK_PIPELINE_CREATE_NO_PROTECTED_ACCESS_BIT;
  PipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  PipelineInfo.stage.module = Module;
  PipelineInfo.stage.pName = "main";
  PipelineInfo.layout = Layout;
  VkPipeline NoProtectedAccessPipeline = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &PipelineInfo,
                                     nullptr, &NoProtectedAccessPipeline),
            VK_SUCCESS);

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(CmdBuf, &BeginInfo), VK_SUCCESS);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE,
                    NoProtectedAccessPipeline);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  ASSERT_EQ(vkEndCommandBuffer(CmdBuf), VK_SUCCESS);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  EXPECT_EQ(Recorded->commands().size(), 2u);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  vkDestroyPipeline(Device, NoProtectedAccessPipeline, nullptr);
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

/// A movable host-mapped `VkBuffer` + its backing `VkDeviceMemory`, used by
/// the buffer-command and storage-buffer-dispatch tests below.
struct HostBuffer {
  VkBuffer Buf = VK_NULL_HANDLE;
  VkDeviceMemory Memory = VK_NULL_HANDLE;
  void *Data = nullptr;
};

TEST_F(CommandBufferTest, CopyBufferCopiesData) {
  HostBuffer Src, Dst;
  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = 16;
  BufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Src.Buf), VK_SUCCESS);
  BufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Dst.Buf), VK_SUCCESS);

  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = 16;
  AllocInfo.memoryTypeIndex = 0;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Src.Memory),
            VK_SUCCESS);
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Dst.Memory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindBufferMemory(Device, Src.Buf, Src.Memory, 0), VK_SUCCESS);
  ASSERT_EQ(vkBindBufferMemory(Device, Dst.Buf, Dst.Memory, 0), VK_SUCCESS);

  ASSERT_EQ(vkMapMemory(Device, Src.Memory, 0, VK_WHOLE_SIZE, 0, &Src.Data),
            VK_SUCCESS);
  uint32_t Payload[4] = {1, 2, 3, 4};
  std::memcpy(Src.Data, Payload, sizeof(Payload));

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  VkBufferCopy Region{0, 0, 16};
  vkCmdCopyBuffer(CmdBuf, Src.Buf, Dst.Buf, 1, &Region);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  void *DstData = nullptr;
  ASSERT_EQ(vkMapMemory(Device, Dst.Memory, 0, VK_WHOLE_SIZE, 0, &DstData),
            VK_SUCCESS);
  EXPECT_EQ(std::memcmp(DstData, Payload, sizeof(Payload)), 0);

  vkDestroyBuffer(Device, Src.Buf, nullptr);
  vkDestroyBuffer(Device, Dst.Buf, nullptr);
  vkFreeMemory(Device, Src.Memory, nullptr);
  vkFreeMemory(Device, Dst.Memory, nullptr);
}

// Roadmap D0: `vkCmdCopyBuffer2` (core in Vulkan 1.3, once this driver
// bumped its advertised apiVersion to 1.4) must produce exactly the copy
// `vkCmdCopyBuffer` already does -- it is the same command with its
// region array wrapped in a `pNext`-extensible info struct instead of
// passed as separate parameters. See "Roadmap D0: measured impact" in
// VulkanCTSReport.md for the crash this closes.
TEST_F(CommandBufferTest, CopyBuffer2CopiesData) {
  HostBuffer Src, Dst;
  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = 16;
  BufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Src.Buf), VK_SUCCESS);
  BufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Dst.Buf), VK_SUCCESS);

  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = 16;
  AllocInfo.memoryTypeIndex = 0;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Src.Memory),
            VK_SUCCESS);
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Dst.Memory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindBufferMemory(Device, Src.Buf, Src.Memory, 0), VK_SUCCESS);
  ASSERT_EQ(vkBindBufferMemory(Device, Dst.Buf, Dst.Memory, 0), VK_SUCCESS);

  ASSERT_EQ(vkMapMemory(Device, Src.Memory, 0, VK_WHOLE_SIZE, 0, &Src.Data),
            VK_SUCCESS);
  uint32_t Payload[4] = {5, 6, 7, 8};
  std::memcpy(Src.Data, Payload, sizeof(Payload));

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  VkBufferCopy2 Region{};
  Region.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
  Region.size = 16;
  VkCopyBufferInfo2 CopyInfo{};
  CopyInfo.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
  CopyInfo.srcBuffer = Src.Buf;
  CopyInfo.dstBuffer = Dst.Buf;
  CopyInfo.regionCount = 1;
  CopyInfo.pRegions = &Region;
  vkCmdCopyBuffer2(CmdBuf, &CopyInfo);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  void *DstData = nullptr;
  ASSERT_EQ(vkMapMemory(Device, Dst.Memory, 0, VK_WHOLE_SIZE, 0, &DstData),
            VK_SUCCESS);
  EXPECT_EQ(std::memcmp(DstData, Payload, sizeof(Payload)), 0);

  vkDestroyBuffer(Device, Src.Buf, nullptr);
  vkDestroyBuffer(Device, Dst.Buf, nullptr);
  vkFreeMemory(Device, Src.Memory, nullptr);
  vkFreeMemory(Device, Dst.Memory, nullptr);
}

TEST_F(CommandBufferTest, FillBufferFillsRegion) {
  HostBuffer Dst;
  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = 16;
  BufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Dst.Buf), VK_SUCCESS);
  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = 16;
  AllocInfo.memoryTypeIndex = 0;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Dst.Memory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindBufferMemory(Device, Dst.Buf, Dst.Memory, 0), VK_SUCCESS);

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdFillBuffer(CmdBuf, Dst.Buf, 0, VK_WHOLE_SIZE, 0xAAAAAAAA);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  uint32_t Words[4];
  ASSERT_EQ(vkMapMemory(Device, Dst.Memory, 0, VK_WHOLE_SIZE, 0, &Dst.Data),
            VK_SUCCESS);
  std::memcpy(Words, Dst.Data, sizeof(Words));
  for (uint32_t W : Words)
    EXPECT_EQ(W, 0xAAAAAAAAu);

  vkDestroyBuffer(Device, Dst.Buf, nullptr);
  vkFreeMemory(Device, Dst.Memory, nullptr);
}

TEST_F(CommandBufferTest, UpdateBufferWritesPayload) {
  HostBuffer Dst;
  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = 8;
  BufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Dst.Buf), VK_SUCCESS);
  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = 8;
  AllocInfo.memoryTypeIndex = 0;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Dst.Memory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindBufferMemory(Device, Dst.Buf, Dst.Memory, 0), VK_SUCCESS);

  uint32_t Payload[2] = {0x11223344, 0x55667788};
  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdUpdateBuffer(CmdBuf, Dst.Buf, 0, sizeof(Payload), Payload);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  ASSERT_EQ(vkMapMemory(Device, Dst.Memory, 0, VK_WHOLE_SIZE, 0, &Dst.Data),
            VK_SUCCESS);
  EXPECT_EQ(std::memcmp(Dst.Data, Payload, sizeof(Payload)), 0);

  vkDestroyBuffer(Device, Dst.Buf, nullptr);
  vkFreeMemory(Device, Dst.Memory, nullptr);
}

TEST_F(CommandBufferTest, PipelineBarrierRecordsAsNoOpJoin) {
  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdPipelineBarrier(CmdBuf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 0, nullptr);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_EQ(Recorded->commands().size(), 3u);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());
}

// Roadmap E3: `vkCmdPipelineBarrier2`'s empty `VkDependencyInfo` (no
// image/buffer/memory barriers) mirrors `PipelineBarrierRecordsAsNoOpJoin`
// above -- recorded the same way its non-`2` counterpart's own no-op join
// is.
TEST_F(CommandBufferTest, PipelineBarrier2RecordsAsNoOpJoin) {
  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  VkDependencyInfo DepInfo{};
  DepInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  vkCmdPipelineBarrier2(CmdBuf, &DepInfo);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_EQ(Recorded->commands().size(), 3u);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());
}

/// End-to-end V2 scenario: bind a descriptor set over two storage buffers,
/// dispatch a shader that reads one and writes the other, and observe the
/// host-visible result -- "run a Vulkan compute shader that reads and
/// writes storage buffers".
class StorageBufferDispatchTest : public ::testing::Test {
protected:
  void SetUp() override {
    VkInstanceCreateInfo InstInfo{};
    ASSERT_EQ(vkCreateInstance(&InstInfo, nullptr, &Instance), VK_SUCCESS);
    uint32_t Count = 1;
    ASSERT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Physical),
              VK_SUCCESS);
    VkDeviceCreateInfo DevInfo{};
    ASSERT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Device), VK_SUCCESS);

    VkDescriptorSetLayoutBinding Bindings[2]{};
    Bindings[0].binding = 0;
    Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[0].descriptorCount = 1;
    Bindings[1].binding = 1;
    Bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    Bindings[1].descriptorCount = 1;
    VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
    SetLayoutInfo.bindingCount = 2;
    SetLayoutInfo.pBindings = Bindings;
    ASSERT_EQ(vkCreateDescriptorSetLayout(Device, &SetLayoutInfo, nullptr,
                                          &SetLayout),
              VK_SUCCESS);

    VkPipelineLayoutCreateInfo LayoutInfo{};
    LayoutInfo.setLayoutCount = 1;
    LayoutInfo.pSetLayouts = &SetLayout;
    ASSERT_EQ(vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &Layout),
              VK_SUCCESS);

    std::vector<uint32_t> Words = assembleSPIRV(kStorageBufferCopyShader);
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

    VkDescriptorPoolSize PoolSizes[2] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1},
    };
    VkDescriptorPoolCreateInfo PoolInfo{};
    PoolInfo.maxSets = 1;
    PoolInfo.poolSizeCount = 2;
    PoolInfo.pPoolSizes = PoolSizes;
    ASSERT_EQ(vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &DescPool),
              VK_SUCCESS);

    VkDescriptorSetAllocateInfo DSAllocInfo{};
    DSAllocInfo.descriptorPool = DescPool;
    DSAllocInfo.descriptorSetCount = 1;
    DSAllocInfo.pSetLayouts = &SetLayout;
    ASSERT_EQ(vkAllocateDescriptorSets(Device, &DSAllocInfo, &Set), VK_SUCCESS);

    VkCommandPoolCreateInfo CmdPoolInfo{};
    CmdPoolInfo.queueFamilyIndex = 0;
    ASSERT_EQ(vkCreateCommandPool(Device, &CmdPoolInfo, nullptr, &Pool),
              VK_SUCCESS);
  }
  void TearDown() override {
    vkDestroyCommandPool(Device, Pool, nullptr);
    vkDestroyDescriptorPool(Device, DescPool, nullptr);
    vkDestroyPipeline(Device, Pipeline, nullptr);
    vkDestroyShaderModule(Device, Module, nullptr);
    vkDestroyPipelineLayout(Device, Layout, nullptr);
    vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
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

  HostBuffer createStorageBuffer(VkDeviceSize Size) {
    HostBuffer Result;
    VkBufferCreateInfo BufferInfo{};
    BufferInfo.size = Size;
    BufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    EXPECT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Result.Buf),
              VK_SUCCESS);
    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.allocationSize = Size;
    AllocInfo.memoryTypeIndex = 0;
    EXPECT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Result.Memory),
              VK_SUCCESS);
    EXPECT_EQ(vkBindBufferMemory(Device, Result.Buf, Result.Memory, 0),
              VK_SUCCESS);
    EXPECT_EQ(
        vkMapMemory(Device, Result.Memory, 0, VK_WHOLE_SIZE, 0, &Result.Data),
        VK_SUCCESS);
    return Result;
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
  VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
  VkPipelineLayout Layout = VK_NULL_HANDLE;
  VkShaderModule Module = VK_NULL_HANDLE;
  VkPipeline Pipeline = VK_NULL_HANDLE;
  VkDescriptorPool DescPool = VK_NULL_HANDLE;
  VkDescriptorSet Set = VK_NULL_HANDLE;
  VkCommandPool Pool = VK_NULL_HANDLE;
};

TEST_F(StorageBufferDispatchTest, ReadsAndWritesThroughBoundDescriptorSet) {
  HostBuffer In = createStorageBuffer(4);
  HostBuffer Out = createStorageBuffer(4);
  uint32_t InitialValue = 41;
  std::memcpy(In.Data, &InitialValue, sizeof(InitialValue));

  VkDescriptorBufferInfo InInfo{In.Buf, 0, 4};
  VkDescriptorBufferInfo OutInfo{Out.Buf, 0, 4};
  VkWriteDescriptorSet Writes[2]{};
  Writes[0].dstSet = Set;
  Writes[0].dstBinding = 0;
  Writes[0].descriptorCount = 1;
  Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Writes[0].pBufferInfo = &InInfo;
  Writes[1].dstSet = Set;
  Writes[1].dstBinding = 1;
  Writes[1].descriptorCount = 1;
  Writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
  Writes[1].pBufferInfo = &OutInfo;
  vkUpdateDescriptorSets(Device, 2, Writes, 0, nullptr);

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  uint32_t DynamicOffset = 0;
  vkCmdBindDescriptorSets(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 1,
                          &Set, 1, &DynamicOffset);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  uint32_t Result = 0;
  std::memcpy(&Result, Out.Data, sizeof(Result));
  EXPECT_EQ(Result, InitialValue + 1);

  vkDestroyBuffer(Device, In.Buf, nullptr);
  vkDestroyBuffer(Device, Out.Buf, nullptr);
  vkFreeMemory(Device, In.Memory, nullptr);
  vkFreeMemory(Device, Out.Memory, nullptr);
}

TEST_F(StorageBufferDispatchTest, DynamicOffsetShiftsBoundBinding) {
  // Two i32 elements; the descriptor declares a 4-byte range starting at
  // buffer offset 0, and the dynamic offset shifts it to the second
  // element -- the write must land there, not at element 0.
  HostBuffer In = createStorageBuffer(4);
  HostBuffer Out = createStorageBuffer(8);
  uint32_t InitialValue = 9;
  std::memcpy(In.Data, &InitialValue, sizeof(InitialValue));
  uint32_t Sentinel[2] = {0xDEADBEEF, 0xDEADBEEF};
  std::memcpy(Out.Data, Sentinel, sizeof(Sentinel));

  VkDescriptorBufferInfo InInfo{In.Buf, 0, 4};
  VkDescriptorBufferInfo OutInfo{Out.Buf, 0, 4};
  VkWriteDescriptorSet Writes[2]{};
  Writes[0].dstSet = Set;
  Writes[0].dstBinding = 0;
  Writes[0].descriptorCount = 1;
  Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Writes[0].pBufferInfo = &InInfo;
  Writes[1].dstSet = Set;
  Writes[1].dstBinding = 1;
  Writes[1].descriptorCount = 1;
  Writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
  Writes[1].pBufferInfo = &OutInfo;
  vkUpdateDescriptorSets(Device, 2, Writes, 0, nullptr);

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  uint32_t DynamicOffset = 4; // Shift binding 1 to the second i32 element.
  vkCmdBindDescriptorSets(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 1,
                          &Set, 1, &DynamicOffset);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  uint32_t Result[2];
  std::memcpy(Result, Out.Data, sizeof(Result));
  EXPECT_EQ(Result[0], 0xDEADBEEFu); // Untouched.
  EXPECT_EQ(Result[1], InitialValue + 1);

  vkDestroyBuffer(Device, In.Buf, nullptr);
  vkDestroyBuffer(Device, Out.Buf, nullptr);
  vkFreeMemory(Device, In.Memory, nullptr);
  vkFreeMemory(Device, Out.Memory, nullptr);
}

/// V4 ("broader ... robustness coverage"): a descriptor's *declared* range
/// (4 bytes -- one element), not the underlying buffer's real size (8
/// bytes -- two), is what bounds every access through it (see "Bounds
/// checking" in feme/docs/FeMeCPUDesign.md). Dispatching two groups reads
/// element 0 (in range) normally but must read element 1 as zero and drop
/// its write entirely, exactly as `robustBufferAccess` requires -- see
/// PhysicalDeviceInfoTest.cpp's `OnlyRobustBufferAccessIsAdvertised` for
/// why this ICD can advertise that feature unconditionally.
TEST_F(StorageBufferDispatchTest,
       OutOfRangeDescriptorAccessReadsZeroAndDropsWrite) {
  HostBuffer In = createStorageBuffer(8);
  HostBuffer Out = createStorageBuffer(8);
  uint32_t InValues[2] = {41, 99};
  std::memcpy(In.Data, InValues, sizeof(InValues));
  uint32_t OutSentinel[2] = {0xDEADBEEF, 0xDEADBEEF};
  std::memcpy(Out.Data, OutSentinel, sizeof(OutSentinel));

  VkDescriptorBufferInfo InInfo{In.Buf, 0, 4};
  VkDescriptorBufferInfo OutInfo{Out.Buf, 0, 4};
  VkWriteDescriptorSet Writes[2]{};
  Writes[0].dstSet = Set;
  Writes[0].dstBinding = 0;
  Writes[0].descriptorCount = 1;
  Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Writes[0].pBufferInfo = &InInfo;
  Writes[1].dstSet = Set;
  Writes[1].dstBinding = 1;
  Writes[1].descriptorCount = 1;
  Writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
  Writes[1].pBufferInfo = &OutInfo;
  vkUpdateDescriptorSets(Device, 2, Writes, 0, nullptr);

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  uint32_t DynamicOffset = 0;
  vkCmdBindDescriptorSets(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 1,
                          &Set, 1, &DynamicOffset);
  vkCmdDispatch(CmdBuf, 2, 1, 1); // GlobalInvocationId.x in {0, 1}.
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  uint32_t Result[2];
  std::memcpy(Result, Out.Data, sizeof(Result));
  EXPECT_EQ(Result[0], InValues[0] + 1); // In range: reads/writes normally.
  EXPECT_EQ(Result[1], OutSentinel[1]);  // Out of range: write dropped.

  vkDestroyBuffer(Device, In.Buf, nullptr);
  vkDestroyBuffer(Device, Out.Buf, nullptr);
  vkFreeMemory(Device, In.Memory, nullptr);
  vkFreeMemory(Device, Out.Memory, nullptr);
}

/// End-to-end V4 scenario ("broader subgroup ... coverage"): compiles and
/// dispatches `kSubgroupBuiltinShader`, observing `SubgroupSize`/
/// `SubgroupLocalInvocationId` through a real `StorageBuffer` write. A
/// single-invocation dispatch (`LocalSize 1, 1, 1`) keeps the expected
/// values simple (lane 0 of a one-lane-active wave) without depending on
/// the host's own pinned wave size for the assertion itself.
TEST_F(CommandBufferTest, SubgroupBuiltinsWriteThroughStorageBuffer) {
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
  VkPipelineLayout SubgroupLayout = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &SubgroupLayout),
      VK_SUCCESS);

  std::vector<uint32_t> Words = assembleSPIRV(kSubgroupBuiltinShader);
  ASSERT_FALSE(Words.empty());
  VkShaderModuleCreateInfo ShaderInfo{};
  ShaderInfo.codeSize = Words.size() * sizeof(uint32_t);
  ShaderInfo.pCode = Words.data();
  VkShaderModule SubgroupModule = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateShaderModule(Device, &ShaderInfo, nullptr, &SubgroupModule),
            VK_SUCCESS);

  VkComputePipelineCreateInfo PipelineInfo{};
  PipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  PipelineInfo.stage.module = SubgroupModule;
  PipelineInfo.stage.pName = "main";
  PipelineInfo.layout = SubgroupLayout;
  VkPipeline SubgroupPipeline = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &PipelineInfo,
                                     nullptr, &SubgroupPipeline),
            VK_SUCCESS);

  VkDescriptorPoolSize PoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
  VkDescriptorPoolCreateInfo PoolInfo{};
  PoolInfo.maxSets = 1;
  PoolInfo.poolSizeCount = 1;
  PoolInfo.pPoolSizes = &PoolSize;
  VkDescriptorPool DescPool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &DescPool),
            VK_SUCCESS);
  VkDescriptorSetAllocateInfo DSAllocInfo{};
  DSAllocInfo.descriptorPool = DescPool;
  DSAllocInfo.descriptorSetCount = 1;
  DSAllocInfo.pSetLayouts = &SetLayout;
  VkDescriptorSet Set = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateDescriptorSets(Device, &DSAllocInfo, &Set), VK_SUCCESS);

  HostBuffer Out;
  VkBufferCreateInfo BufferInfo{};
  BufferInfo.size = 8;
  BufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Out.Buf), VK_SUCCESS);
  VkMemoryAllocateInfo AllocInfo{};
  AllocInfo.allocationSize = 8;
  AllocInfo.memoryTypeIndex = 0;
  ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Out.Memory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindBufferMemory(Device, Out.Buf, Out.Memory, 0), VK_SUCCESS);
  ASSERT_EQ(vkMapMemory(Device, Out.Memory, 0, VK_WHOLE_SIZE, 0, &Out.Data),
            VK_SUCCESS);

  VkDescriptorBufferInfo OutInfo{Out.Buf, 0, 8};
  VkWriteDescriptorSet Write{};
  Write.dstSet = Set;
  Write.dstBinding = 0;
  Write.descriptorCount = 1;
  Write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Write.pBufferInfo = &OutInfo;
  vkUpdateDescriptorSets(Device, 1, &Write, 0, nullptr);

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, SubgroupPipeline);
  vkCmdBindDescriptorSets(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE,
                          SubgroupLayout, 0, 1, &Set, 0, nullptr);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  uint32_t Result[2];
  std::memcpy(Result, Out.Data, sizeof(Result));
  EXPECT_GE(Result[0], 1u); // SubgroupSize: at least one lane.
  EXPECT_EQ(Result[1], 0u); // SubgroupLocalInvocationId: the only invocation.

  vkDestroyBuffer(Device, Out.Buf, nullptr);
  vkFreeMemory(Device, Out.Memory, nullptr);
  vkDestroyDescriptorPool(Device, DescPool, nullptr);
  vkDestroyPipeline(Device, SubgroupPipeline, nullptr);
  vkDestroyShaderModule(Device, SubgroupModule, nullptr);
  vkDestroyPipelineLayout(Device, SubgroupLayout, nullptr);
  vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
}

/// (roadmap F12) End-to-end `VK_KHR_push_descriptor` scenario: no
/// `VkDescriptorPool`/`VkDescriptorSet` object exists at all -- every write
/// goes straight into the command buffer's own recorded state through
/// `vkCmdPushDescriptorSet`/`vkCmdPushDescriptorSetWithTemplate`, sharing
/// `kStorageBufferCopyShader`'s own two-binding (`in`/`out` storage buffer)
/// layout with `StorageBufferDispatchTest` above.
class PushDescriptorSetDispatchTest : public ::testing::Test {
protected:
  void SetUp() override {
    VkInstanceCreateInfo InstInfo{};
    ASSERT_EQ(vkCreateInstance(&InstInfo, nullptr, &Instance), VK_SUCCESS);
    uint32_t Count = 1;
    ASSERT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Physical),
              VK_SUCCESS);
    VkDeviceCreateInfo DevInfo{};
    ASSERT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Device), VK_SUCCESS);

    VkDescriptorSetLayoutBinding Bindings[2]{};
    Bindings[0].binding = 0;
    Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[0].descriptorCount = 1;
    Bindings[1].binding = 1;
    Bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[1].descriptorCount = 1;
    VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
    SetLayoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT;
    SetLayoutInfo.bindingCount = 2;
    SetLayoutInfo.pBindings = Bindings;
    ASSERT_EQ(vkCreateDescriptorSetLayout(Device, &SetLayoutInfo, nullptr,
                                          &SetLayout),
              VK_SUCCESS);

    VkPipelineLayoutCreateInfo LayoutInfo{};
    LayoutInfo.setLayoutCount = 1;
    LayoutInfo.pSetLayouts = &SetLayout;
    ASSERT_EQ(vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &Layout),
              VK_SUCCESS);

    std::vector<uint32_t> Words = assembleSPIRV(kStorageBufferCopyShader);
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

    VkCommandPoolCreateInfo CmdPoolInfo{};
    CmdPoolInfo.queueFamilyIndex = 0;
    ASSERT_EQ(vkCreateCommandPool(Device, &CmdPoolInfo, nullptr, &Pool),
              VK_SUCCESS);
  }
  void TearDown() override {
    vkDestroyCommandPool(Device, Pool, nullptr);
    vkDestroyPipeline(Device, Pipeline, nullptr);
    vkDestroyShaderModule(Device, Module, nullptr);
    vkDestroyPipelineLayout(Device, Layout, nullptr);
    vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
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

  HostBuffer createStorageBuffer(VkDeviceSize Size) {
    HostBuffer Result;
    VkBufferCreateInfo BufferInfo{};
    BufferInfo.size = Size;
    BufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    EXPECT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Result.Buf),
              VK_SUCCESS);
    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.allocationSize = Size;
    AllocInfo.memoryTypeIndex = 0;
    EXPECT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Result.Memory),
              VK_SUCCESS);
    EXPECT_EQ(vkBindBufferMemory(Device, Result.Buf, Result.Memory, 0),
              VK_SUCCESS);
    EXPECT_EQ(
        vkMapMemory(Device, Result.Memory, 0, VK_WHOLE_SIZE, 0, &Result.Data),
        VK_SUCCESS);
    return Result;
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
  VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
  VkPipelineLayout Layout = VK_NULL_HANDLE;
  VkShaderModule Module = VK_NULL_HANDLE;
  VkPipeline Pipeline = VK_NULL_HANDLE;
  VkCommandPool Pool = VK_NULL_HANDLE;
};

TEST_F(PushDescriptorSetDispatchTest, ReadsAndWritesWithNoDescriptorSetObject) {
  HostBuffer In = createStorageBuffer(4);
  HostBuffer Out = createStorageBuffer(4);
  uint32_t InitialValue = 41;
  std::memcpy(In.Data, &InitialValue, sizeof(InitialValue));

  VkDescriptorBufferInfo InInfo{In.Buf, 0, 4};
  VkDescriptorBufferInfo OutInfo{Out.Buf, 0, 4};
  VkWriteDescriptorSet Writes[2]{};
  Writes[0].dstBinding = 0;
  Writes[0].descriptorCount = 1;
  Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Writes[0].pBufferInfo = &InInfo;
  Writes[1].dstBinding = 1;
  Writes[1].descriptorCount = 1;
  Writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Writes[1].pBufferInfo = &OutInfo;

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdPushDescriptorSet(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 2,
                         Writes);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  uint32_t Result = 0;
  std::memcpy(&Result, Out.Data, sizeof(Result));
  EXPECT_EQ(Result, InitialValue + 1);

  vkDestroyBuffer(Device, In.Buf, nullptr);
  vkDestroyBuffer(Device, Out.Buf, nullptr);
  vkFreeMemory(Device, In.Memory, nullptr);
  vkFreeMemory(Device, Out.Memory, nullptr);
}

/// Per `vkCmdPushDescriptorSet`'s own spec text, pushes "can be updated
/// incrementally": a second push to the same slot that only rewrites
/// binding 1 must leave binding 0's earlier write in place, rather than the
/// whole slot reverting to undefined -- matching VK-GL-CTS's own
/// `PushDescriptorIncrementalUpdatesComputeTest` scenario.
TEST_F(PushDescriptorSetDispatchTest,
       WritesToOneBindingAccumulateAcrossPushes) {
  HostBuffer In = createStorageBuffer(4);
  HostBuffer Out1 = createStorageBuffer(4);
  HostBuffer Out2 = createStorageBuffer(4);
  uint32_t InitialValue = 7;
  std::memcpy(In.Data, &InitialValue, sizeof(InitialValue));

  VkDescriptorBufferInfo InInfo{In.Buf, 0, 4};
  VkDescriptorBufferInfo Out1Info{Out1.Buf, 0, 4};
  VkDescriptorBufferInfo Out2Info{Out2.Buf, 0, 4};
  VkWriteDescriptorSet FirstWrites[2]{};
  FirstWrites[0].dstBinding = 0;
  FirstWrites[0].descriptorCount = 1;
  FirstWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  FirstWrites[0].pBufferInfo = &InInfo;
  FirstWrites[1].dstBinding = 1;
  FirstWrites[1].descriptorCount = 1;
  FirstWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  FirstWrites[1].pBufferInfo = &Out1Info;
  VkWriteDescriptorSet SecondWrite{};
  SecondWrite.dstBinding = 1;
  SecondWrite.descriptorCount = 1;
  SecondWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  SecondWrite.pBufferInfo = &Out2Info;

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdPushDescriptorSet(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 2,
                         FirstWrites);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  // Only binding 1 is rewritten; binding 0 (the `in` buffer) must still be
  // whatever the first push left there.
  vkCmdPushDescriptorSet(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 1,
                         &SecondWrite);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  uint32_t Result1 = 0, Result2 = 0;
  std::memcpy(&Result1, Out1.Data, sizeof(Result1));
  std::memcpy(&Result2, Out2.Data, sizeof(Result2));
  EXPECT_EQ(Result1, InitialValue + 1);
  EXPECT_EQ(Result2, InitialValue + 1);

  vkDestroyBuffer(Device, In.Buf, nullptr);
  vkDestroyBuffer(Device, Out1.Buf, nullptr);
  vkDestroyBuffer(Device, Out2.Buf, nullptr);
  vkFreeMemory(Device, In.Memory, nullptr);
  vkFreeMemory(Device, Out1.Memory, nullptr);
  vkFreeMemory(Device, Out2.Memory, nullptr);
}

TEST_F(PushDescriptorSetDispatchTest, WithTemplateReadsAndWrites) {
  HostBuffer In = createStorageBuffer(4);
  HostBuffer Out = createStorageBuffer(4);
  uint32_t InitialValue = 99;
  std::memcpy(In.Data, &InitialValue, sizeof(InitialValue));

  VkDescriptorUpdateTemplateEntry Entries[2] = {
      {0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0,
       sizeof(VkDescriptorBufferInfo)},
      {1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
       sizeof(VkDescriptorBufferInfo), sizeof(VkDescriptorBufferInfo)},
  };
  VkDescriptorUpdateTemplateCreateInfo TemplateInfo{};
  TemplateInfo.descriptorUpdateEntryCount = 2;
  TemplateInfo.pDescriptorUpdateEntries = Entries;
  TemplateInfo.templateType =
      VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS;
  VkDescriptorUpdateTemplate Template = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDescriptorUpdateTemplate(Device, &TemplateInfo, nullptr,
                                             &Template),
            VK_SUCCESS);

  struct {
    VkDescriptorBufferInfo In;
    VkDescriptorBufferInfo Out;
  } Data{{In.Buf, 0, 4}, {Out.Buf, 0, 4}};

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdPushDescriptorSetWithTemplate(CmdBuf, Template, Layout, 0, &Data);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  uint32_t Result = 0;
  std::memcpy(&Result, Out.Data, sizeof(Result));
  EXPECT_EQ(Result, InitialValue + 1);

  vkDestroyDescriptorUpdateTemplate(Device, Template, nullptr);
  vkDestroyBuffer(Device, In.Buf, nullptr);
  vkDestroyBuffer(Device, Out.Buf, nullptr);
  vkFreeMemory(Device, In.Memory, nullptr);
  vkFreeMemory(Device, Out.Memory, nullptr);
}

TEST_F(PushDescriptorSetDispatchTest, PushDescriptorSet2ProducesTheSameResult) {
  HostBuffer In = createStorageBuffer(4);
  HostBuffer Out = createStorageBuffer(4);
  uint32_t InitialValue = 12;
  std::memcpy(In.Data, &InitialValue, sizeof(InitialValue));

  VkDescriptorBufferInfo InInfo{In.Buf, 0, 4};
  VkDescriptorBufferInfo OutInfo{Out.Buf, 0, 4};
  VkWriteDescriptorSet Writes[2]{};
  Writes[0].dstBinding = 0;
  Writes[0].descriptorCount = 1;
  Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Writes[0].pBufferInfo = &InInfo;
  Writes[1].dstBinding = 1;
  Writes[1].descriptorCount = 1;
  Writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Writes[1].pBufferInfo = &OutInfo;

  VkPushDescriptorSetInfo PushInfo{};
  PushInfo.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  PushInfo.layout = Layout;
  PushInfo.set = 0;
  PushInfo.descriptorWriteCount = 2;
  PushInfo.pDescriptorWrites = Writes;

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdPushDescriptorSet2(CmdBuf, &PushInfo);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  uint32_t Result = 0;
  std::memcpy(&Result, Out.Data, sizeof(Result));
  EXPECT_EQ(Result, InitialValue + 1);

  vkDestroyBuffer(Device, In.Buf, nullptr);
  vkDestroyBuffer(Device, Out.Buf, nullptr);
  vkFreeMemory(Device, In.Memory, nullptr);
  vkFreeMemory(Device, Out.Memory, nullptr);
}

/// (roadmap F12) "When a command buffer begins recording, all push
/// descriptors are undefined" (`vkCmdPushDescriptorSet`'s own spec text):
/// re-beginning a command buffer must drop a previously-pushed binding
/// entirely, not merely leave it stale -- pushing only binding 1 the
/// second time around must see binding 0 (`in`) read back as zero, the
/// same "unwritten descriptor reads zero" behavior an ordinary,
/// never-written `DescriptorSet` binding already has (see
/// `StorageBufferDispatchTest`'s file-level scope), rather than the first
/// recording's own `in` value leaking through.
TEST_F(PushDescriptorSetDispatchTest,
       BeginCommandBufferDropsEveryPushDescriptorSet) {
  HostBuffer In = createStorageBuffer(4);
  HostBuffer Out = createStorageBuffer(4);
  uint32_t InitialValue = 5;
  std::memcpy(In.Data, &InitialValue, sizeof(InitialValue));

  VkDescriptorBufferInfo InInfo{In.Buf, 0, 4};
  VkDescriptorBufferInfo OutInfo{Out.Buf, 0, 4};
  VkWriteDescriptorSet Writes[2]{};
  Writes[0].dstBinding = 0;
  Writes[0].descriptorCount = 1;
  Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Writes[0].pBufferInfo = &InInfo;
  Writes[1].dstBinding = 1;
  Writes[1].descriptorCount = 1;
  Writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Writes[1].pBufferInfo = &OutInfo;

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdPushDescriptorSet(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 2,
                         Writes);
  vkEndCommandBuffer(CmdBuf);

  // Re-begin and push only binding 1 this time; if binding 0's push from
  // the recording above survived, `in` would still read `InitialValue`
  // instead of the zero an unwritten binding produces.
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdPushDescriptorSet(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 1,
                         &Writes[1]);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  uint32_t Result = 0;
  std::memcpy(&Result, Out.Data, sizeof(Result));
  EXPECT_EQ(Result, 0u + 1); // in reads zero: not the earlier InitialValue.

  vkDestroyBuffer(Device, In.Buf, nullptr);
  vkDestroyBuffer(Device, Out.Buf, nullptr);
  vkFreeMemory(Device, In.Memory, nullptr);
  vkFreeMemory(Device, Out.Memory, nullptr);
}

/// End-to-end V4 scenario: bind a uniform texel buffer and a storage texel
/// buffer over `VK_FORMAT_R32G32B32A32_SFLOAT` `VkBufferView`s, dispatch a
/// shader that reads one texel, adds a constant, and writes it to the
/// other, and observe the host-visible result -- "Implement uniform/storage
/// texel buffers" (see Descriptor.h's file comment for this milestone's
/// format scope).
class TexelBufferDispatchTest : public ::testing::Test {
protected:
  /// The shader `SetUp` assembles and dispatches; overridden by
  /// `IntTexelBufferDispatchTest` below to exercise the `<4 x i32>` shape
  /// instead of the default `<4 x float>` one.
  virtual const char *getShaderSource() { return kTexelBufferAddShader; }

  void SetUp() override {
    VkInstanceCreateInfo InstInfo{};
    ASSERT_EQ(vkCreateInstance(&InstInfo, nullptr, &Instance), VK_SUCCESS);
    uint32_t Count = 1;
    ASSERT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Physical),
              VK_SUCCESS);
    VkDeviceCreateInfo DevInfo{};
    ASSERT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Device), VK_SUCCESS);

    VkDescriptorSetLayoutBinding Bindings[2]{};
    Bindings[0].binding = 0;
    Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    Bindings[0].descriptorCount = 1;
    Bindings[1].binding = 1;
    Bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    Bindings[1].descriptorCount = 1;
    VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
    SetLayoutInfo.bindingCount = 2;
    SetLayoutInfo.pBindings = Bindings;
    ASSERT_EQ(vkCreateDescriptorSetLayout(Device, &SetLayoutInfo, nullptr,
                                          &SetLayout),
              VK_SUCCESS);

    VkPipelineLayoutCreateInfo LayoutInfo{};
    LayoutInfo.setLayoutCount = 1;
    LayoutInfo.pSetLayouts = &SetLayout;
    ASSERT_EQ(vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &Layout),
              VK_SUCCESS);

    std::vector<uint32_t> Words = assembleSPIRV(getShaderSource());
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

    VkDescriptorPoolSize PoolSizes[2] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1},
    };
    VkDescriptorPoolCreateInfo PoolInfo{};
    PoolInfo.maxSets = 1;
    PoolInfo.poolSizeCount = 2;
    PoolInfo.pPoolSizes = PoolSizes;
    ASSERT_EQ(vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &DescPool),
              VK_SUCCESS);

    VkDescriptorSetAllocateInfo DSAllocInfo{};
    DSAllocInfo.descriptorPool = DescPool;
    DSAllocInfo.descriptorSetCount = 1;
    DSAllocInfo.pSetLayouts = &SetLayout;
    ASSERT_EQ(vkAllocateDescriptorSets(Device, &DSAllocInfo, &Set), VK_SUCCESS);

    VkCommandPoolCreateInfo CmdPoolInfo{};
    CmdPoolInfo.queueFamilyIndex = 0;
    ASSERT_EQ(vkCreateCommandPool(Device, &CmdPoolInfo, nullptr, &Pool),
              VK_SUCCESS);
  }
  void TearDown() override {
    vkDestroyCommandPool(Device, Pool, nullptr);
    vkDestroyDescriptorPool(Device, DescPool, nullptr);
    vkDestroyPipeline(Device, Pipeline, nullptr);
    vkDestroyShaderModule(Device, Module, nullptr);
    vkDestroyPipelineLayout(Device, Layout, nullptr);
    vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
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

  HostBuffer createTexelBuffer(VkDeviceSize Size) {
    HostBuffer Result;
    VkBufferCreateInfo BufferInfo{};
    BufferInfo.size = Size;
    BufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
                       VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
    EXPECT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Result.Buf),
              VK_SUCCESS);
    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.allocationSize = Size;
    AllocInfo.memoryTypeIndex = 0;
    EXPECT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Result.Memory),
              VK_SUCCESS);
    EXPECT_EQ(vkBindBufferMemory(Device, Result.Buf, Result.Memory, 0),
              VK_SUCCESS);
    EXPECT_EQ(
        vkMapMemory(Device, Result.Memory, 0, VK_WHOLE_SIZE, 0, &Result.Data),
        VK_SUCCESS);
    return Result;
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
  VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
  VkPipelineLayout Layout = VK_NULL_HANDLE;
  VkShaderModule Module = VK_NULL_HANDLE;
  VkPipeline Pipeline = VK_NULL_HANDLE;
  VkDescriptorPool DescPool = VK_NULL_HANDLE;
  VkDescriptorSet Set = VK_NULL_HANDLE;
  VkCommandPool Pool = VK_NULL_HANDLE;
};

TEST_F(TexelBufferDispatchTest, ReadsAndWritesThroughBoundBufferViews) {
  HostBuffer In = createTexelBuffer(16); // One <4 x float> texel.
  HostBuffer Out = createTexelBuffer(16);
  float InitialValue[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  std::memcpy(In.Data, InitialValue, sizeof(InitialValue));

  VkBufferViewCreateInfo InViewInfo{};
  InViewInfo.buffer = In.Buf;
  InViewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
  InViewInfo.range = VK_WHOLE_SIZE;
  VkBufferView InView = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBufferView(Device, &InViewInfo, nullptr, &InView),
            VK_SUCCESS);
  VkBufferViewCreateInfo OutViewInfo{};
  OutViewInfo.buffer = Out.Buf;
  OutViewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
  OutViewInfo.range = VK_WHOLE_SIZE;
  VkBufferView OutView = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBufferView(Device, &OutViewInfo, nullptr, &OutView),
            VK_SUCCESS);

  VkWriteDescriptorSet Writes[2]{};
  Writes[0].dstSet = Set;
  Writes[0].dstBinding = 0;
  Writes[0].descriptorCount = 1;
  Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
  Writes[0].pTexelBufferView = &InView;
  Writes[1].dstSet = Set;
  Writes[1].dstBinding = 1;
  Writes[1].descriptorCount = 1;
  Writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
  Writes[1].pTexelBufferView = &OutView;
  vkUpdateDescriptorSets(Device, 2, Writes, 0, nullptr);

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdBindDescriptorSets(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 1,
                          &Set, 0, nullptr);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  float Result[4];
  std::memcpy(Result, Out.Data, sizeof(Result));
  EXPECT_FLOAT_EQ(Result[0], InitialValue[0] + 1.0f);
  EXPECT_FLOAT_EQ(Result[1], InitialValue[1] + 1.0f);
  EXPECT_FLOAT_EQ(Result[2], InitialValue[2] + 1.0f);
  EXPECT_FLOAT_EQ(Result[3], InitialValue[3] + 1.0f);

  vkDestroyBufferView(Device, InView, nullptr);
  vkDestroyBufferView(Device, OutView, nullptr);
  vkDestroyBuffer(Device, In.Buf, nullptr);
  vkDestroyBuffer(Device, Out.Buf, nullptr);
  vkFreeMemory(Device, In.Memory, nullptr);
  vkFreeMemory(Device, Out.Memory, nullptr);
}

/// V4 follow-up: the packed `R8G8B8A8_SNORM` counterpart of
/// `ReadsAndWritesThroughBoundBufferViews` above, reusing the same
/// `<4 x float>` shader (`kTexelBufferAddShader`) and only swapping the
/// bound `VkFormat` and buffer size (one packed `uint32_t` texel instead of
/// one `<4 x float>` texel) -- proving `femeCpuResourceLoadTypedV4F32`/
/// `StoreTypedV4F32`'s `R8G8B8A8_SNORM` conversion end to end through a
/// real compiled-and-dispatched SPIR-V shader.
TEST_F(TexelBufferDispatchTest, ReadsAndWritesThroughSnormBufferViews) {
  HostBuffer In = createTexelBuffer(4); // One packed R8G8B8A8_SNORM texel.
  HostBuffer Out = createTexelBuffer(4);
  // R=1.0 (0x7F), G=-1.0 (0x81), B=0.5 (~0x40), A=-0.5 (~0xC0).
  uint32_t InitialValue = 0xC0u << 24 | 0x40u << 16 | 0x81u << 8 | 0x7Fu;
  std::memcpy(In.Data, &InitialValue, sizeof(InitialValue));

  VkBufferViewCreateInfo InViewInfo{};
  InViewInfo.buffer = In.Buf;
  InViewInfo.format = VK_FORMAT_R8G8B8A8_SNORM;
  InViewInfo.range = VK_WHOLE_SIZE;
  VkBufferView InView = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBufferView(Device, &InViewInfo, nullptr, &InView),
            VK_SUCCESS);
  VkBufferViewCreateInfo OutViewInfo{};
  OutViewInfo.buffer = Out.Buf;
  OutViewInfo.format = VK_FORMAT_R8G8B8A8_SNORM;
  OutViewInfo.range = VK_WHOLE_SIZE;
  VkBufferView OutView = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBufferView(Device, &OutViewInfo, nullptr, &OutView),
            VK_SUCCESS);

  VkWriteDescriptorSet Writes[2]{};
  Writes[0].dstSet = Set;
  Writes[0].dstBinding = 0;
  Writes[0].descriptorCount = 1;
  Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
  Writes[0].pTexelBufferView = &InView;
  Writes[1].dstSet = Set;
  Writes[1].dstBinding = 1;
  Writes[1].descriptorCount = 1;
  Writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
  Writes[1].pTexelBufferView = &OutView;
  vkUpdateDescriptorSets(Device, 2, Writes, 0, nullptr);

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdBindDescriptorSets(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 1,
                          &Set, 0, nullptr);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  // The shader adds 1.0 to every lane before writing back; SNORM clamps
  // the result to [-1.0, 1.0]: R (1.0+1.0) and B (~0.504+1.0) both
  // saturate to 1.0 (127), G (-1.0+1.0) lands exactly at 0.0 (0), and A
  // (~-0.504+1.0) lands at ~0.496 (63).
  uint32_t RawResult;
  std::memcpy(&RawResult, Out.Data, sizeof(RawResult));
  int8_t R = (int8_t)(uint8_t)RawResult;
  int8_t G = (int8_t)(uint8_t)(RawResult >> 8);
  int8_t B = (int8_t)(uint8_t)(RawResult >> 16);
  int8_t A = (int8_t)(uint8_t)(RawResult >> 24);
  EXPECT_EQ(R, 127);
  EXPECT_EQ(G, 0);
  EXPECT_EQ(B, 127);
  EXPECT_NEAR(A, 63, 1);

  vkDestroyBufferView(Device, InView, nullptr);
  vkDestroyBufferView(Device, OutView, nullptr);
  vkDestroyBuffer(Device, In.Buf, nullptr);
  vkDestroyBuffer(Device, Out.Buf, nullptr);
  vkFreeMemory(Device, In.Memory, nullptr);
  vkFreeMemory(Device, Out.Memory, nullptr);
}

/// `TexelBufferDispatchTest` above -- see `kIntTexelBufferAddShader`'s
/// comment. Reuses the base fixture's object-model setup, only swapping the
/// dispatched shader.
class IntTexelBufferDispatchTest : public TexelBufferDispatchTest {
protected:
  const char *getShaderSource() override { return kIntTexelBufferAddShader; }
};

TEST_F(IntTexelBufferDispatchTest, ReadsAndWritesThroughIntegerBufferViews) {
  HostBuffer In = createTexelBuffer(16); // One <4 x i32> texel.
  HostBuffer Out = createTexelBuffer(16);
  int32_t InitialValue[4] = {1, -2, 3, -4};
  std::memcpy(In.Data, InitialValue, sizeof(InitialValue));

  VkBufferViewCreateInfo InViewInfo{};
  InViewInfo.buffer = In.Buf;
  InViewInfo.format = VK_FORMAT_R32G32B32A32_UINT;
  InViewInfo.range = VK_WHOLE_SIZE;
  VkBufferView InView = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBufferView(Device, &InViewInfo, nullptr, &InView),
            VK_SUCCESS);
  VkBufferViewCreateInfo OutViewInfo{};
  OutViewInfo.buffer = Out.Buf;
  OutViewInfo.format = VK_FORMAT_R32G32B32A32_SINT;
  OutViewInfo.range = VK_WHOLE_SIZE;
  VkBufferView OutView = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBufferView(Device, &OutViewInfo, nullptr, &OutView),
            VK_SUCCESS);

  VkWriteDescriptorSet Writes[2]{};
  Writes[0].dstSet = Set;
  Writes[0].dstBinding = 0;
  Writes[0].descriptorCount = 1;
  Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
  Writes[0].pTexelBufferView = &InView;
  Writes[1].dstSet = Set;
  Writes[1].dstBinding = 1;
  Writes[1].descriptorCount = 1;
  Writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
  Writes[1].pTexelBufferView = &OutView;
  vkUpdateDescriptorSets(Device, 2, Writes, 0, nullptr);

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdBindDescriptorSets(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 1,
                          &Set, 0, nullptr);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  int32_t Result[4];
  std::memcpy(Result, Out.Data, sizeof(Result));
  EXPECT_EQ(Result[0], InitialValue[0] + 1);
  EXPECT_EQ(Result[1], InitialValue[1] + 1);
  EXPECT_EQ(Result[2], InitialValue[2] + 1);
  EXPECT_EQ(Result[3], InitialValue[3] + 1);

  vkDestroyBufferView(Device, InView, nullptr);
  vkDestroyBufferView(Device, OutView, nullptr);
  vkDestroyBuffer(Device, In.Buf, nullptr);
  vkDestroyBuffer(Device, Out.Buf, nullptr);
  vkFreeMemory(Device, In.Memory, nullptr);
  vkFreeMemory(Device, Out.Memory, nullptr);
}

/// V4 follow-up: the packed `R8G8B8A8_UINT`/`_SINT` counterpart of
/// `ReadsAndWritesThroughIntegerBufferViews` above -- same
/// `kIntTexelBufferAddShader`, only bound over the packed 8-bit-per-
/// component formats (one packed `uint32_t` texel each) instead of the
/// 32-bit identity ones, proving `femeCpuResourceLoadTypedV4I32`/
/// `StoreTypedV4I32`'s packed-byte conversion end to end.
TEST_F(IntTexelBufferDispatchTest, ReadsAndWritesThroughPackedByteBufferViews) {
  HostBuffer In = createTexelBuffer(4);  // One packed R8G8B8A8_UINT texel.
  HostBuffer Out = createTexelBuffer(4); // One packed R8G8B8A8_SINT texel.
  uint8_t InitialValue[4] = {1, 200, 3, 250};
  std::memcpy(In.Data, InitialValue, sizeof(InitialValue));

  VkBufferViewCreateInfo InViewInfo{};
  InViewInfo.buffer = In.Buf;
  InViewInfo.format = VK_FORMAT_R8G8B8A8_UINT;
  InViewInfo.range = VK_WHOLE_SIZE;
  VkBufferView InView = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBufferView(Device, &InViewInfo, nullptr, &InView),
            VK_SUCCESS);
  VkBufferViewCreateInfo OutViewInfo{};
  OutViewInfo.buffer = Out.Buf;
  OutViewInfo.format = VK_FORMAT_R8G8B8A8_SINT;
  OutViewInfo.range = VK_WHOLE_SIZE;
  VkBufferView OutView = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateBufferView(Device, &OutViewInfo, nullptr, &OutView),
            VK_SUCCESS);

  VkWriteDescriptorSet Writes[2]{};
  Writes[0].dstSet = Set;
  Writes[0].dstBinding = 0;
  Writes[0].descriptorCount = 1;
  Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
  Writes[0].pTexelBufferView = &InView;
  Writes[1].dstSet = Set;
  Writes[1].dstBinding = 1;
  Writes[1].descriptorCount = 1;
  Writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
  Writes[1].pTexelBufferView = &OutView;
  vkUpdateDescriptorSets(Device, 2, Writes, 0, nullptr);

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdBindDescriptorSets(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 1,
                          &Set, 0, nullptr);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  // Read as unsigned bytes (255 + 1 == 0 read as UINT), zero-extended by
  // the load, incremented, and truncated back to one byte on store --
  // matching plain modular byte arithmetic, not signed saturation.
  int8_t Result[4];
  std::memcpy(Result, Out.Data, sizeof(Result));
  EXPECT_EQ(Result[0], 2);
  EXPECT_EQ((uint8_t)Result[1], 201u);
  EXPECT_EQ(Result[2], 4);
  EXPECT_EQ((uint8_t)Result[3], 251u);

  vkDestroyBufferView(Device, InView, nullptr);
  vkDestroyBufferView(Device, OutView, nullptr);
  vkDestroyBuffer(Device, In.Buf, nullptr);
  vkDestroyBuffer(Device, Out.Buf, nullptr);
  vkFreeMemory(Device, In.Memory, nullptr);
  vkFreeMemory(Device, Out.Memory, nullptr);
}

class UniformBufferDispatchTest : public ::testing::Test {
protected:
  void SetUp() override {
    VkInstanceCreateInfo InstInfo{};
    ASSERT_EQ(vkCreateInstance(&InstInfo, nullptr, &Instance), VK_SUCCESS);
    uint32_t Count = 1;
    ASSERT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Physical),
              VK_SUCCESS);
    VkDeviceCreateInfo DevInfo{};
    ASSERT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Device), VK_SUCCESS);

    VkDescriptorSetLayoutBinding Bindings[2]{};
    Bindings[0].binding = 0;
    Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    Bindings[0].descriptorCount = 1;
    Bindings[1].binding = 1;
    Bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[1].descriptorCount = 1;
    VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
    SetLayoutInfo.bindingCount = 2;
    SetLayoutInfo.pBindings = Bindings;
    ASSERT_EQ(vkCreateDescriptorSetLayout(Device, &SetLayoutInfo, nullptr,
                                          &SetLayout),
              VK_SUCCESS);

    VkPipelineLayoutCreateInfo LayoutInfo{};
    LayoutInfo.setLayoutCount = 1;
    LayoutInfo.pSetLayouts = &SetLayout;
    ASSERT_EQ(vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &Layout),
              VK_SUCCESS);

    std::vector<uint32_t> Words = assembleSPIRV(kUniformBufferReadShader);
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

    VkDescriptorPoolSize PoolSizes[2] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
    };
    VkDescriptorPoolCreateInfo PoolInfo{};
    PoolInfo.maxSets = 1;
    PoolInfo.poolSizeCount = 2;
    PoolInfo.pPoolSizes = PoolSizes;
    ASSERT_EQ(vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &DescPool),
              VK_SUCCESS);

    VkDescriptorSetAllocateInfo DSAllocInfo{};
    DSAllocInfo.descriptorPool = DescPool;
    DSAllocInfo.descriptorSetCount = 1;
    DSAllocInfo.pSetLayouts = &SetLayout;
    ASSERT_EQ(vkAllocateDescriptorSets(Device, &DSAllocInfo, &Set), VK_SUCCESS);

    VkCommandPoolCreateInfo CmdPoolInfo{};
    CmdPoolInfo.queueFamilyIndex = 0;
    ASSERT_EQ(vkCreateCommandPool(Device, &CmdPoolInfo, nullptr, &Pool),
              VK_SUCCESS);
  }
  void TearDown() override {
    vkDestroyCommandPool(Device, Pool, nullptr);
    vkDestroyDescriptorPool(Device, DescPool, nullptr);
    vkDestroyPipeline(Device, Pipeline, nullptr);
    vkDestroyShaderModule(Device, Module, nullptr);
    vkDestroyPipelineLayout(Device, Layout, nullptr);
    vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
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

  HostBuffer createBuffer(VkDeviceSize Size, VkBufferUsageFlags Usage) {
    HostBuffer Result;
    VkBufferCreateInfo BufferInfo{};
    BufferInfo.size = Size;
    BufferInfo.usage = Usage;
    EXPECT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Result.Buf),
              VK_SUCCESS);
    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.allocationSize = Size;
    AllocInfo.memoryTypeIndex = 0;
    EXPECT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Result.Memory),
              VK_SUCCESS);
    EXPECT_EQ(vkBindBufferMemory(Device, Result.Buf, Result.Memory, 0),
              VK_SUCCESS);
    EXPECT_EQ(
        vkMapMemory(Device, Result.Memory, 0, VK_WHOLE_SIZE, 0, &Result.Data),
        VK_SUCCESS);
    return Result;
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
  VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
  VkPipelineLayout Layout = VK_NULL_HANDLE;
  VkShaderModule Module = VK_NULL_HANDLE;
  VkPipeline Pipeline = VK_NULL_HANDLE;
  VkDescriptorPool DescPool = VK_NULL_HANDLE;
  VkDescriptorSet Set = VK_NULL_HANDLE;
  VkCommandPool Pool = VK_NULL_HANDLE;
};

TEST_F(UniformBufferDispatchTest, ReadsSecondFieldThroughBoundDescriptorSet) {
  HostBuffer Cb = createBuffer(8, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
  HostBuffer Out = createBuffer(4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  uint32_t CbFields[2] = {10, 20};
  std::memcpy(Cb.Data, CbFields, sizeof(CbFields));

  VkDescriptorBufferInfo CbInfo{Cb.Buf, 0, 8};
  VkDescriptorBufferInfo OutInfo{Out.Buf, 0, 4};
  VkWriteDescriptorSet Writes[2]{};
  Writes[0].dstSet = Set;
  Writes[0].dstBinding = 0;
  Writes[0].descriptorCount = 1;
  Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  Writes[0].pBufferInfo = &CbInfo;
  Writes[1].dstSet = Set;
  Writes[1].dstBinding = 1;
  Writes[1].descriptorCount = 1;
  Writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Writes[1].pBufferInfo = &OutInfo;
  vkUpdateDescriptorSets(Device, 2, Writes, 0, nullptr);

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdBindDescriptorSets(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 1,
                          &Set, 0, nullptr);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  uint32_t Result = 0;
  std::memcpy(&Result, Out.Data, sizeof(Result));
  EXPECT_EQ(Result, CbFields[1]); // The second field, not the first.

  vkDestroyBuffer(Device, Cb.Buf, nullptr);
  vkDestroyBuffer(Device, Out.Buf, nullptr);
  vkFreeMemory(Device, Cb.Memory, nullptr);
  vkFreeMemory(Device, Out.Memory, nullptr);
}

class PushConstantDispatchTest : public ::testing::Test {
protected:
  void SetUp() override {
    VkInstanceCreateInfo InstInfo{};
    ASSERT_EQ(vkCreateInstance(&InstInfo, nullptr, &Instance), VK_SUCCESS);
    uint32_t Count = 1;
    ASSERT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Physical),
              VK_SUCCESS);
    VkDeviceCreateInfo DevInfo{};
    ASSERT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Device), VK_SUCCESS);

    VkDescriptorSetLayoutBinding Binding{};
    Binding.binding = 0;
    Binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Binding.descriptorCount = 1;
    VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
    SetLayoutInfo.bindingCount = 1;
    SetLayoutInfo.pBindings = &Binding;
    ASSERT_EQ(vkCreateDescriptorSetLayout(Device, &SetLayoutInfo, nullptr,
                                          &SetLayout),
              VK_SUCCESS);

    VkPushConstantRange Range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
    VkPipelineLayoutCreateInfo LayoutInfo{};
    LayoutInfo.setLayoutCount = 1;
    LayoutInfo.pSetLayouts = &SetLayout;
    LayoutInfo.pushConstantRangeCount = 1;
    LayoutInfo.pPushConstantRanges = &Range;
    ASSERT_EQ(vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &Layout),
              VK_SUCCESS);

    std::vector<uint32_t> Words = assembleSPIRV(kPushConstantAddShader);
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

    VkDescriptorPoolSize PoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo PoolInfo{};
    PoolInfo.maxSets = 1;
    PoolInfo.poolSizeCount = 1;
    PoolInfo.pPoolSizes = &PoolSize;
    ASSERT_EQ(vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &DescPool),
              VK_SUCCESS);

    VkDescriptorSetAllocateInfo DSAllocInfo{};
    DSAllocInfo.descriptorPool = DescPool;
    DSAllocInfo.descriptorSetCount = 1;
    DSAllocInfo.pSetLayouts = &SetLayout;
    ASSERT_EQ(vkAllocateDescriptorSets(Device, &DSAllocInfo, &Set), VK_SUCCESS);

    VkCommandPoolCreateInfo CmdPoolInfo{};
    CmdPoolInfo.queueFamilyIndex = 0;
    ASSERT_EQ(vkCreateCommandPool(Device, &CmdPoolInfo, nullptr, &Pool),
              VK_SUCCESS);
  }
  void TearDown() override {
    vkDestroyCommandPool(Device, Pool, nullptr);
    vkDestroyDescriptorPool(Device, DescPool, nullptr);
    vkDestroyPipeline(Device, Pipeline, nullptr);
    vkDestroyShaderModule(Device, Module, nullptr);
    vkDestroyPipelineLayout(Device, Layout, nullptr);
    vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
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

  HostBuffer createStorageBuffer(VkDeviceSize Size) {
    HostBuffer Result;
    VkBufferCreateInfo BufferInfo{};
    BufferInfo.size = Size;
    BufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    EXPECT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Result.Buf),
              VK_SUCCESS);
    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.allocationSize = Size;
    AllocInfo.memoryTypeIndex = 0;
    EXPECT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Result.Memory),
              VK_SUCCESS);
    EXPECT_EQ(vkBindBufferMemory(Device, Result.Buf, Result.Memory, 0),
              VK_SUCCESS);
    EXPECT_EQ(
        vkMapMemory(Device, Result.Memory, 0, VK_WHOLE_SIZE, 0, &Result.Data),
        VK_SUCCESS);
    return Result;
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
  VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
  VkPipelineLayout Layout = VK_NULL_HANDLE;
  VkShaderModule Module = VK_NULL_HANDLE;
  VkPipeline Pipeline = VK_NULL_HANDLE;
  VkDescriptorPool DescPool = VK_NULL_HANDLE;
  VkDescriptorSet Set = VK_NULL_HANDLE;
  VkCommandPool Pool = VK_NULL_HANDLE;
};

TEST_F(PushConstantDispatchTest, PushedBytesReachTheDispatch) {
  HostBuffer Buf = createStorageBuffer(4);
  uint32_t InitialValue = 10;
  std::memcpy(Buf.Data, &InitialValue, sizeof(InitialValue));

  VkDescriptorBufferInfo BufInfo{Buf.Buf, 0, 4};
  VkWriteDescriptorSet Write{};
  Write.dstSet = Set;
  Write.dstBinding = 0;
  Write.descriptorCount = 1;
  Write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Write.pBufferInfo = &BufInfo;
  vkUpdateDescriptorSets(Device, 1, &Write, 0, nullptr);

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdBindDescriptorSets(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 1,
                          &Set, 0, nullptr);
  uint32_t PushValue = 32;
  vkCmdPushConstants(CmdBuf, Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     sizeof(PushValue), &PushValue);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  uint32_t Result = 0;
  std::memcpy(&Result, Buf.Data, sizeof(Result));
  EXPECT_EQ(Result, InitialValue + PushValue);

  vkDestroyBuffer(Device, Buf.Buf, nullptr);
  vkFreeMemory(Device, Buf.Memory, nullptr);
}

TEST_F(PushConstantDispatchTest, UnpushedBytesReadAsZero) {
  // No `vkCmdPushConstants` call at all: the command buffer's push-constant
  // state starts zero-initialized (see "Descriptor Model"'s "declared but
  // never written" convention), so the shader observes 0.
  HostBuffer Buf = createStorageBuffer(4);
  uint32_t InitialValue = 10;
  std::memcpy(Buf.Data, &InitialValue, sizeof(InitialValue));

  VkDescriptorBufferInfo BufInfo{Buf.Buf, 0, 4};
  VkWriteDescriptorSet Write{};
  Write.dstSet = Set;
  Write.dstBinding = 0;
  Write.descriptorCount = 1;
  Write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Write.pBufferInfo = &BufInfo;
  vkUpdateDescriptorSets(Device, 1, &Write, 0, nullptr);

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdBindDescriptorSets(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 1,
                          &Set, 0, nullptr);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  uint32_t Result = 0;
  std::memcpy(&Result, Buf.Data, sizeof(Result));
  EXPECT_EQ(Result, InitialValue);

  vkDestroyBuffer(Device, Buf.Buf, nullptr);
  vkFreeMemory(Device, Buf.Memory, nullptr);
}

// Roadmap E6: `VK_KHR_maintenance6`'s `vkCmdBindDescriptorSets2`/
// `vkCmdPushConstants2` are shape-compatible `pNext`-extensible wrappers
// around `vkCmdBindDescriptorSets`/`vkCmdPushConstants`; this exercises both
// together through the same dispatch the non-`2` equivalents above already
// cover, confirming they reach the identical `CommandBuffer` state.
TEST_F(PushConstantDispatchTest,
       BindDescriptorSets2AndPushConstants2ReachTheDispatch) {
  HostBuffer Buf = createStorageBuffer(4);
  uint32_t InitialValue = 10;
  std::memcpy(Buf.Data, &InitialValue, sizeof(InitialValue));

  VkDescriptorBufferInfo BufInfo{Buf.Buf, 0, 4};
  VkWriteDescriptorSet Write{};
  Write.dstSet = Set;
  Write.dstBinding = 0;
  Write.descriptorCount = 1;
  Write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Write.pBufferInfo = &BufInfo;
  vkUpdateDescriptorSets(Device, 1, &Write, 0, nullptr);

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);

  VkBindDescriptorSetsInfo BindInfo{};
  BindInfo.sType = VK_STRUCTURE_TYPE_BIND_DESCRIPTOR_SETS_INFO;
  BindInfo.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  BindInfo.layout = Layout;
  BindInfo.firstSet = 0;
  BindInfo.descriptorSetCount = 1;
  BindInfo.pDescriptorSets = &Set;
  BindInfo.dynamicOffsetCount = 0;
  BindInfo.pDynamicOffsets = nullptr;
  vkCmdBindDescriptorSets2(CmdBuf, &BindInfo);

  uint32_t PushValue = 32;
  VkPushConstantsInfo PushInfo{};
  PushInfo.sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO;
  PushInfo.layout = Layout;
  PushInfo.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  PushInfo.offset = 0;
  PushInfo.size = sizeof(PushValue);
  PushInfo.pValues = &PushValue;
  vkCmdPushConstants2(CmdBuf, &PushInfo);

  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  uint32_t Result = 0;
  std::memcpy(&Result, Buf.Data, sizeof(Result));
  EXPECT_EQ(Result, InitialValue + PushValue);

  vkDestroyBuffer(Device, Buf.Buf, nullptr);
  vkFreeMemory(Device, Buf.Memory, nullptr);
}

TEST_F(CommandBufferTest, QueryPoolWriteTimestampThenGetResults) {
  VkQueryPoolCreateInfo PoolInfo{};
  PoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
  PoolInfo.queryCount = 2;
  VkQueryPool QPool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateQueryPool(Device, &PoolInfo, nullptr, &QPool), VK_SUCCESS);

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdResetQueryPool(CmdBuf, QPool, 0, 2);
  vkCmdWriteTimestamp(CmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, QPool, 0);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  // stride must fit one entry: value + availability flag, when
  // VK_QUERY_RESULT_WITH_AVAILABILITY_BIT is set.
  uint64_t Results[4] = {0xDEADBEEFDEADBEEFull, 0xDEADBEEFDEADBEEFull,
                         0xDEADBEEFDEADBEEFull, 0xDEADBEEFDEADBEEFull};
  EXPECT_EQ(vkGetQueryPoolResults(Device, QPool, 0, 2, sizeof(Results), Results,
                                  2 * sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT |
                                      VK_QUERY_RESULT_WITH_AVAILABILITY_BIT),
            VK_NOT_READY); // Query 1 is not yet available.
  // Every value this ICD reports is zero (see QueryPool.h's file comment);
  // only the availability flag distinguishes query 0 (written) from query
  // 1 (reset but never written).
  EXPECT_EQ(Results[0], 0u); // Query 0's value.
  EXPECT_EQ(Results[1], 1u); // Query 0's availability: available.
  EXPECT_EQ(Results[2], 0u); // Query 1's value.
  EXPECT_EQ(Results[3], 0u); // Query 1's availability: unavailable.

  uint64_t Availability[2] = {0, 0};
  EXPECT_EQ(vkGetQueryPoolResults(Device, QPool, 0, 2, sizeof(Availability),
                                  Availability, sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT),
            VK_NOT_READY); // Query 1 is still unavailable.

  vkDestroyQueryPool(Device, QPool, nullptr);
}

// Roadmap E3: `vkCmdWriteTimestamp2`'s 2-stage-mask `stage` argument
// mirrors `QueryPoolWriteTimestampThenGetResults` above.
TEST_F(CommandBufferTest, QueryPoolWriteTimestamp2ThenGetResults) {
  VkQueryPoolCreateInfo PoolInfo{};
  PoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
  PoolInfo.queryCount = 1;
  VkQueryPool QPool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateQueryPool(Device, &PoolInfo, nullptr, &QPool), VK_SUCCESS);

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdResetQueryPool(CmdBuf, QPool, 0, 1);
  vkCmdWriteTimestamp2(CmdBuf, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, QPool, 0);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  uint64_t Results[2] = {0xDEADBEEFDEADBEEFull, 0xDEADBEEFDEADBEEFull};
  EXPECT_EQ(vkGetQueryPoolResults(
                Device, QPool, 0, 1, sizeof(Results), Results, sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT),
            VK_SUCCESS);
  EXPECT_EQ(Results[0], 0u); // Query 0's value.
  EXPECT_EQ(Results[1], 1u); // Query 0's availability: available.

  vkDestroyQueryPool(Device, QPool, nullptr);
}

TEST_F(CommandBufferTest, ExecuteCommandsInterpretsSecondaryIntoPrimary) {
  VkCommandBufferAllocateInfo SecondaryAllocInfo{};
  SecondaryAllocInfo.commandPool = Pool;
  SecondaryAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
  SecondaryAllocInfo.commandBufferCount = 1;
  VkCommandBuffer Secondary = VK_NULL_HANDLE;
  ASSERT_EQ(vkAllocateCommandBuffers(Device, &SecondaryAllocInfo, &Secondary),
            VK_SUCCESS);
  EXPECT_EQ(fromHandle<CommandBuffer>(Secondary)->level(),
            VK_COMMAND_BUFFER_LEVEL_SECONDARY);

  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(Secondary, &BeginInfo);
  vkCmdBindPipeline(Secondary, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdDispatch(Secondary, 1, 1, 1);
  vkEndCommandBuffer(Secondary);

  VkCommandBuffer Primary = allocateCommandBuffer();
  vkBeginCommandBuffer(Primary, &BeginInfo);
  vkCmdExecuteCommands(Primary, 1, &Secondary);
  vkEndCommandBuffer(Primary);

  auto *Recorded = fromHandle<CommandBuffer>(Primary);
  ASSERT_EQ(Recorded->commands().size(), 1u);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());
}

} // namespace

namespace {

/// (Roadmap R30 / V5) A dispatch that samples a bound 2x2
/// `R32G32B32A32_SFLOAT` image through a bound sampler and writes the
/// sampled texel to a storage buffer. This is the scenario V5's status note
/// recorded as its one remaining deviation ("a real dispatch still cannot
/// consume an image or sampler").
class SampledImageDispatchTest : public ::testing::Test {
protected:
  static constexpr uint32_t Extent = 2;
  static constexpr VkDeviceSize TexelSize = 16; // R32G32B32A32_SFLOAT
  static constexpr VkDeviceSize ImageSize = Extent * Extent * TexelSize;

  void SetUp() override {
    VkInstanceCreateInfo InstInfo{};
    ASSERT_EQ(vkCreateInstance(&InstInfo, nullptr, &Instance), VK_SUCCESS);
    uint32_t Count = 1;
    ASSERT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Physical),
              VK_SUCCESS);
    VkDeviceCreateInfo DevInfo{};
    ASSERT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Device), VK_SUCCESS);

    createImage();
    createSampler();
    createOutputBuffer();

    VkDescriptorSetLayoutBinding Bindings[3]{};
    Bindings[0].binding = 0;
    Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    Bindings[0].descriptorCount = 1;
    Bindings[1].binding = 1;
    Bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    Bindings[1].descriptorCount = 1;
    Bindings[2].binding = 2;
    Bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[2].descriptorCount = 1;
    VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
    SetLayoutInfo.bindingCount = 3;
    SetLayoutInfo.pBindings = Bindings;
    ASSERT_EQ(vkCreateDescriptorSetLayout(Device, &SetLayoutInfo, nullptr,
                                          &SetLayout),
              VK_SUCCESS);

    VkPipelineLayoutCreateInfo LayoutInfo{};
    LayoutInfo.setLayoutCount = 1;
    LayoutInfo.pSetLayouts = &SetLayout;
    ASSERT_EQ(vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &Layout),
              VK_SUCCESS);

    std::vector<uint32_t> Words = assembleSPIRV(kSampledImageShader);
    ASSERT_FALSE(Words.empty());
    VkShaderModuleCreateInfo ShaderInfo{};
    ShaderInfo.codeSize = Words.size() * sizeof(uint32_t);
    ShaderInfo.pCode = Words.data();
    ASSERT_EQ(vkCreateShaderModule(Device, &ShaderInfo, nullptr, &Module),
              VK_SUCCESS);

    VkDescriptorPoolSize PoolSizes[3] = {
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
    };
    VkDescriptorPoolCreateInfo PoolInfo{};
    PoolInfo.maxSets = 1;
    PoolInfo.poolSizeCount = 3;
    PoolInfo.pPoolSizes = PoolSizes;
    ASSERT_EQ(vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &DescPool),
              VK_SUCCESS);

    VkDescriptorSetAllocateInfo DSAllocInfo{};
    DSAllocInfo.descriptorPool = DescPool;
    DSAllocInfo.descriptorSetCount = 1;
    DSAllocInfo.pSetLayouts = &SetLayout;
    ASSERT_EQ(vkAllocateDescriptorSets(Device, &DSAllocInfo, &Set), VK_SUCCESS);

    VkCommandPoolCreateInfo CmdPoolInfo{};
    CmdPoolInfo.queueFamilyIndex = 0;
    ASSERT_EQ(vkCreateCommandPool(Device, &CmdPoolInfo, nullptr, &Pool),
              VK_SUCCESS);
  }

  void TearDown() override {
    vkDestroyCommandPool(Device, Pool, nullptr);
    vkDestroyDescriptorPool(Device, DescPool, nullptr);
    vkDestroyPipeline(Device, Pipeline, nullptr);
    vkDestroyShaderModule(Device, Module, nullptr);
    vkDestroyPipelineLayout(Device, Layout, nullptr);
    vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
    vkDestroySampler(Device, Samp, nullptr);
    vkDestroyImageView(Device, View, nullptr);
    vkDestroyImage(Device, Img, nullptr);
    vkFreeMemory(Device, ImageMemory, nullptr);
    vkDestroyBuffer(Device, Out.Buf, nullptr);
    vkFreeMemory(Device, Out.Memory, nullptr);
    vkDestroyDevice(Device, nullptr);
    vkDestroyInstance(Instance, nullptr);
  }

  virtual void createImage() {
    VkImageCreateInfo ImageInfo{};
    ImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ImageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    ImageInfo.extent = {Extent, Extent, 1};
    ImageInfo.mipLevels = 1;
    ImageInfo.arrayLayers = 1;
    ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ImageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &Img), VK_SUCCESS);

    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.allocationSize = ImageSize;
    AllocInfo.memoryTypeIndex = 0;
    ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &ImageMemory),
              VK_SUCCESS);
    ASSERT_EQ(vkBindImageMemory(Device, Img, ImageMemory, 0), VK_SUCCESS);
    ASSERT_EQ(vkMapMemory(Device, ImageMemory, 0, VK_WHOLE_SIZE, 0, &Texels),
              VK_SUCCESS);

    // Each texel carries its own linear index in every channel, so a wrong
    // row/column resolves to an obviously wrong value rather than to a
    // plausible neighbor.
    auto *F = static_cast<float *>(Texels);
    for (uint32_t I = 0; I != Extent * Extent; ++I)
      for (uint32_t C = 0; C != 4; ++C)
        F[I * 4 + C] = static_cast<float>(I * 4 + C);

    VkImageViewCreateInfo ViewInfo{};
    ViewInfo.image = Img;
    ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ViewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewInfo.subresourceRange.levelCount = 1;
    ViewInfo.subresourceRange.layerCount = 1;
    ASSERT_EQ(vkCreateImageView(Device, &ViewInfo, nullptr, &View), VK_SUCCESS);
  }

  void createSampler() {
    VkSamplerCreateInfo SamplerInfo{};
    SamplerInfo.magFilter = VK_FILTER_NEAREST;
    SamplerInfo.minFilter = VK_FILTER_NEAREST;
    SamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ASSERT_EQ(vkCreateSampler(Device, &SamplerInfo, nullptr, &Samp),
              VK_SUCCESS);
  }

  void createOutputBuffer() {
    VkBufferCreateInfo BufferInfo{};
    BufferInfo.size = 4 * sizeof(float);
    BufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    ASSERT_EQ(vkCreateBuffer(Device, &BufferInfo, nullptr, &Out.Buf),
              VK_SUCCESS);
    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.allocationSize = BufferInfo.size;
    AllocInfo.memoryTypeIndex = 0;
    ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &Out.Memory),
              VK_SUCCESS);
    ASSERT_EQ(vkBindBufferMemory(Device, Out.Buf, Out.Memory, 0), VK_SUCCESS);
    ASSERT_EQ(vkMapMemory(Device, Out.Memory, 0, VK_WHOLE_SIZE, 0, &Out.Data),
              VK_SUCCESS);
  }

  VkResult createPipeline() {
    VkComputePipelineCreateInfo PipelineInfo{};
    PipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    PipelineInfo.stage.module = Module;
    PipelineInfo.stage.pName = "main";
    PipelineInfo.layout = Layout;
    return vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &PipelineInfo,
                                    nullptr, &Pipeline);
  }

  void writeDescriptorSet() {
    VkDescriptorImageInfo ImgInfo{};
    ImgInfo.imageView = View;
    ImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo SampInfo{};
    SampInfo.sampler = Samp;
    VkDescriptorBufferInfo OutInfo{Out.Buf, 0, 4 * sizeof(float)};

    VkWriteDescriptorSet Writes[3]{};
    Writes[0].dstSet = Set;
    Writes[0].dstBinding = 0;
    Writes[0].descriptorCount = 1;
    Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    Writes[0].pImageInfo = &ImgInfo;
    Writes[1].dstSet = Set;
    Writes[1].dstBinding = 1;
    Writes[1].descriptorCount = 1;
    Writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    Writes[1].pImageInfo = &SampInfo;
    Writes[2].dstSet = Set;
    Writes[2].dstBinding = 2;
    Writes[2].descriptorCount = 1;
    Writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[2].pBufferInfo = &OutInfo;
    vkUpdateDescriptorSets(Device, 3, Writes, 0, nullptr);
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
  VkImage Img = VK_NULL_HANDLE;
  VkDeviceMemory ImageMemory = VK_NULL_HANDLE;
  void *Texels = nullptr;
  VkImageView View = VK_NULL_HANDLE;
  VkSampler Samp = VK_NULL_HANDLE;
  HostBuffer Out;
  VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
  VkPipelineLayout Layout = VK_NULL_HANDLE;
  VkShaderModule Module = VK_NULL_HANDLE;
  VkPipeline Pipeline = VK_NULL_HANDLE;
  VkDescriptorPool DescPool = VK_NULL_HANDLE;
  VkDescriptorSet Set = VK_NULL_HANDLE;
  VkCommandPool Pool = VK_NULL_HANDLE;
};

} // namespace

TEST_F(SampledImageDispatchTest, PipelineWithASamplerIsNoLongerRejected) {
  // V5 rejected any shader whose reflection set `UsesSamplerHeap`; there is
  // now a real image/sampler heap for it to bind against.
  EXPECT_EQ(createPipeline(), VK_SUCCESS);
}

TEST_F(SampledImageDispatchTest, SamplesABoundImageThroughABoundSampler) {
  ASSERT_EQ(createPipeline(), VK_SUCCESS);
  writeDescriptorSet();

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdBindDescriptorSets(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 1,
                          &Set, 0, nullptr);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  // Nearest sampling at uv (0.75, 0.75) of a 2x2 image selects texel
  // (1, 1), i.e. linear index 3, whose channels the fixture filled with
  // 12, 13, 14, 15.
  float Result[4] = {};
  std::memcpy(Result, Out.Data, sizeof(Result));
  EXPECT_FLOAT_EQ(Result[0], 12.0f);
  EXPECT_FLOAT_EQ(Result[1], 13.0f);
  EXPECT_FLOAT_EQ(Result[2], 14.0f);
  EXPECT_FLOAT_EQ(Result[3], 15.0f);
}

TEST_F(SampledImageDispatchTest, UnwrittenImageBindingSamplesAsZero) {
  // Only the sampler and the output buffer are written; the image binding
  // is left unwritten, so its heap slot stays the all-zero descriptor and
  // the sample reads the robust zero result rather than a wild pointer.
  ASSERT_EQ(createPipeline(), VK_SUCCESS);
  VkDescriptorImageInfo SampInfo{};
  SampInfo.sampler = Samp;
  VkDescriptorBufferInfo OutInfo{Out.Buf, 0, 4 * sizeof(float)};
  VkWriteDescriptorSet Writes[2]{};
  Writes[0].dstSet = Set;
  Writes[0].dstBinding = 1;
  Writes[0].descriptorCount = 1;
  Writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  Writes[0].pImageInfo = &SampInfo;
  Writes[1].dstSet = Set;
  Writes[1].dstBinding = 2;
  Writes[1].descriptorCount = 1;
  Writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Writes[1].pBufferInfo = &OutInfo;
  vkUpdateDescriptorSets(Device, 2, Writes, 0, nullptr);

  auto *Sentinel = static_cast<float *>(Out.Data);
  for (uint32_t I = 0; I != 4; ++I)
    Sentinel[I] = -1.0f;

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdBindDescriptorSets(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 1,
                          &Set, 0, nullptr);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  float Result[4] = {};
  std::memcpy(Result, Out.Data, sizeof(Result));
  for (float Component : Result)
    EXPECT_FLOAT_EQ(Component, 0.0f);
}

TEST_F(SampledImageDispatchTest, RejectsAPipelineLayoutOfTheWrongClass) {
  // The shader samples through (set 0, binding 0), so a layout declaring a
  // storage buffer there cannot satisfy it: the compiler assigned that
  // range an *image* heap slot, which no buffer descriptor can fill.
  VkDescriptorSetLayoutBinding Bindings[3]{};
  Bindings[0].binding = 0;
  Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Bindings[0].descriptorCount = 1;
  Bindings[1].binding = 1;
  Bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  Bindings[1].descriptorCount = 1;
  Bindings[2].binding = 2;
  Bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Bindings[2].descriptorCount = 1;
  VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
  SetLayoutInfo.bindingCount = 3;
  SetLayoutInfo.pBindings = Bindings;
  VkDescriptorSetLayout WrongSetLayout = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateDescriptorSetLayout(Device, &SetLayoutInfo, nullptr,
                                        &WrongSetLayout),
            VK_SUCCESS);
  VkPipelineLayoutCreateInfo LayoutInfo{};
  LayoutInfo.setLayoutCount = 1;
  LayoutInfo.pSetLayouts = &WrongSetLayout;
  VkPipelineLayout WrongLayout = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &WrongLayout),
            VK_SUCCESS);

  VkComputePipelineCreateInfo PipelineInfo{};
  PipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  PipelineInfo.stage.module = Module;
  PipelineInfo.stage.pName = "main";
  PipelineInfo.layout = WrongLayout;
  VkPipeline Rejected = VK_NULL_HANDLE;
  EXPECT_NE(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &PipelineInfo,
                                     nullptr, &Rejected),
            VK_SUCCESS);

  vkDestroyPipelineLayout(Device, WrongLayout, nullptr);
  vkDestroyDescriptorSetLayout(Device, WrongSetLayout, nullptr);
}

namespace {

/// Sets bits `[Start, Start + Len)` of \p Block (bit 0 the LSB of byte 0,
/// matching ASTCDecode.cpp's own convention -- see ASTCDecodeTest.cpp's
/// identical helper) to \p Value's low \p Len bits, used here only to
/// hand-construct one void-extent (solid-fill) ASTC block for
/// `ASTCSampledImageDispatchTest`.
void setASTCBits(uint8_t Block[16], unsigned Start, unsigned Len,
                 uint32_t Value) {
  for (unsigned I = 0; I != Len; ++I) {
    unsigned BitIndex = Start + I;
    unsigned Byte = BitIndex / 8, Bit = BitIndex % 8;
    if ((Value >> I) & 1)
      Block[Byte] |= uint8_t(1u << Bit);
    else
      Block[Byte] &= uint8_t(~(1u << Bit));
  }
}

/// (Roadmap E23) The same scenario `SampledImageDispatchTest` exercises --
/// a dispatch sampling a bound image through a bound sampler -- but with a
/// single-block `VK_FORMAT_ASTC_4x4_UNORM_BLOCK` image in place of an
/// uncompressed one, verifying `materializeImageDescriptor`'s new decode
/// path actually reaches a shader rather than the previous all-zero read.
/// Only `createImage` differs, so this reuses every other fixture method
/// (pipeline/descriptor/command-buffer setup, `kSampledImageShader`) as
/// is.
class ASTCSampledImageDispatchTest : public SampledImageDispatchTest {
protected:
  void createImage() override {
    VkImageCreateInfo ImageInfo{};
    ImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ImageInfo.format = VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
    ImageInfo.extent = {4, 4, 1};
    ImageInfo.mipLevels = 1;
    ImageInfo.arrayLayers = 1;
    ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ImageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &Img), VK_SUCCESS);

    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.allocationSize = 16; // One 4x4 ASTC block, 128 bits.
    AllocInfo.memoryTypeIndex = 0;
    ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &ImageMemory),
              VK_SUCCESS);
    ASSERT_EQ(vkBindImageMemory(Device, Img, ImageMemory, 0), VK_SUCCESS);
    ASSERT_EQ(vkMapMemory(Device, ImageMemory, 0, VK_WHOLE_SIZE, 0, &Texels),
              VK_SUCCESS);

    // A void-extent (solid-fill) block -- the same bit layout
    // ASTCDecodeTest.cpp's `VoidExtentDecodesToSolidColor` test uses --
    // decodes to R=255, G=0, B=127 (32767 -> 255 scale, truncated), A=255
    // at every one of the block's 16 texels, so the exact (u, v) this
    // fixture's shader samples at does not matter.
    auto *Bytes = static_cast<uint8_t *>(Texels);
    std::memset(Bytes, 0, 16);
    setASTCBits(Bytes, 0, 9, 0x1FC);
    setASTCBits(Bytes, 9, 1, 0); // LDR.
    setASTCBits(Bytes, 10, 2, 0x3);
    setASTCBits(Bytes, 64, 16, 65535);
    setASTCBits(Bytes, 80, 16, 0);
    setASTCBits(Bytes, 96, 16, 32767);
    setASTCBits(Bytes, 112, 16, 65535);

    VkImageViewCreateInfo ViewInfo{};
    ViewInfo.image = Img;
    ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ViewInfo.format = VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
    ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewInfo.subresourceRange.levelCount = 1;
    ViewInfo.subresourceRange.layerCount = 1;
    ASSERT_EQ(vkCreateImageView(Device, &ViewInfo, nullptr, &View), VK_SUCCESS);
  }
};

} // namespace

TEST_F(ASTCSampledImageDispatchTest,
       SamplesARealDecodedTexelRatherThanAllZero) {
  ASSERT_EQ(createPipeline(), VK_SUCCESS);
  writeDescriptorSet();

  VkCommandBuffer CmdBuf = allocateCommandBuffer();
  VkCommandBufferBeginInfo BeginInfo{};
  vkBeginCommandBuffer(CmdBuf, &BeginInfo);
  vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
  vkCmdBindDescriptorSets(CmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 1,
                          &Set, 0, nullptr);
  vkCmdDispatch(CmdBuf, 1, 1, 1);
  vkEndCommandBuffer(CmdBuf);

  auto *Recorded = fromHandle<CommandBuffer>(CmdBuf);
  ASSERT_THAT_ERROR(executeCommandBuffer(*Recorded), llvm::Succeeded());

  float Result[4] = {};
  std::memcpy(Result, Out.Data, sizeof(Result));
  EXPECT_FLOAT_EQ(Result[0], 1.0f);
  EXPECT_FLOAT_EQ(Result[1], 0.0f);
  EXPECT_FLOAT_EQ(Result[2], 127.0f / 255.0f);
  EXPECT_FLOAT_EQ(Result[3], 1.0f);
}
