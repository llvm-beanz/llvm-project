//===- DrawTest.cpp - End-to-end V6 draw tests --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// (V6) The milestone's own end-to-end scenario: render off-screen through a
// `VkRenderPass`, from real SPIR-V vertex/fragment modules, and observe the
// resulting image -- the whole path from `vkCmdBeginRenderPass` through the
// normalized render-target binding, the prepared draw, and the software
// graphics executor (see "Draw commands and vertex data" in
// feme/docs/FeMeVulkanDesign.md).
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "Buffer.h"
#include "CommandBuffer.h"
#include "EntryPoints.h"
#include "GraphicsPipeline.h"
#include "Icd.h"
#include "Image.h"
#include "Objects.h"
#include "RenderPass.h"

#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Target/SPIRV/Serialization.h"

#include "gtest/gtest.h"

#include <cstring>
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
  llvm::SmallVector<uint32_t, 0> Binary;
  if (mlir::failed(mlir::spirv::serialize(*Module, Binary)))
    return {};
  return std::vector<uint32_t>(Binary.begin(), Binary.end());
}

/// One oversized counter-clockwise triangle selected from `gl_VertexIndex`,
/// covering the whole viewport after clipping.
constexpr llvm::StringLiteral FullscreenVertexSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @vid built_in("VertexIndex") : !spirv.ptr<i32, Input>
  spirv.GlobalVariable @pos built_in("Position") : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %vidp = spirv.mlir.addressof @vid : !spirv.ptr<i32, Input>
    %v = spirv.Load "Input" %vidp : i32
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %is0 = spirv.IEqual %v, %c0 : i32
    %is1 = spirv.IEqual %v, %c1 : i32
    %neg1 = spirv.Constant -1.0 : f32
    %three = spirv.Constant 3.0 : f32
    %xb = spirv.Select %is1, %three, %neg1 : i1, f32
    %x = spirv.Select %is0, %neg1, %xb : i1, f32
    %yb = spirv.Select %is1, %neg1, %three : i1, f32
    %y = spirv.Select %is0, %neg1, %yb : i1, f32
    %z = spirv.Constant 0.0 : f32
    %w = spirv.Constant 1.0 : f32
    %p = spirv.CompositeConstruct %x, %y, %z, %w : (f32, f32, f32, f32) -> vector<4xf32>
    %posp = spirv.mlir.addressof @pos : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %posp, %p : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @main, @vid, @pos
}
)mlir";

/// Roadmap H3: the same oversized fullscreen triangle as
/// `FullscreenVertexSource`, plus a `ViewportIndex` output set to the
/// primitive's own `InstanceIndex` -- routes instance 0 to viewport/scissor
/// 0 and instance 1 to viewport/scissor 1, with no geometry stage (this ICD
/// implements none), matching `VK_EXT_shader_viewport_index_layer`'s own
/// "any shader stage" relaxation of the classic geometry-shader-only rule.
constexpr llvm::StringLiteral FullscreenVertexWithInstanceViewportSource =
    R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, ShaderViewportIndexLayerEXT], [SPV_EXT_shader_viewport_index_layer]> {
  spirv.GlobalVariable @vid built_in("VertexIndex") : !spirv.ptr<i32, Input>
  spirv.GlobalVariable @iid built_in("InstanceIndex") : !spirv.ptr<i32, Input>
  spirv.GlobalVariable @pos built_in("Position") : !spirv.ptr<vector<4xf32>, Output>
  spirv.GlobalVariable @vp built_in("ViewportIndex") : !spirv.ptr<i32, Output>
  spirv.func @main() -> () "None" {
    %vidp = spirv.mlir.addressof @vid : !spirv.ptr<i32, Input>
    %v = spirv.Load "Input" %vidp : i32
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %is0 = spirv.IEqual %v, %c0 : i32
    %is1 = spirv.IEqual %v, %c1 : i32
    %neg1 = spirv.Constant -1.0 : f32
    %three = spirv.Constant 3.0 : f32
    %xb = spirv.Select %is1, %three, %neg1 : i1, f32
    %x = spirv.Select %is0, %neg1, %xb : i1, f32
    %yb = spirv.Select %is1, %neg1, %three : i1, f32
    %y = spirv.Select %is0, %neg1, %yb : i1, f32
    %z = spirv.Constant 0.0 : f32
    %w = spirv.Constant 1.0 : f32
    %p = spirv.CompositeConstruct %x, %y, %z, %w : (f32, f32, f32, f32) -> vector<4xf32>
    %posp = spirv.mlir.addressof @pos : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %posp, %p : vector<4xf32>
    %iidp = spirv.mlir.addressof @iid : !spirv.ptr<i32, Input>
    %inst = spirv.Load "Input" %iidp : i32
    %vpp = spirv.mlir.addressof @vp : !spirv.ptr<i32, Output>
    spirv.Store "Output" %vpp, %inst : i32
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @main, @vid, @iid, @pos, @vp
}
)mlir";

/// Solid red into SV_Target0.
constexpr llvm::StringLiteral RedFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %c = spirv.Constant dense<[1.0, 0.0, 0.0, 1.0]> : vector<4xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p, %c : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir";

/// (roadmap H6f) A minimal mesh entry point: declares its output topology/
/// count execution modes and workgroup size but emits nothing (no
/// `spirv.EmitMeshTasksEXT`/per-vertex writes -- real mesh output content
/// is blocked on roadmap H6h/H6i). Enough to exercise
/// `vkCmdDrawMeshTasksEXT`/`vkCmdDrawMeshTasksIndirectEXT`/
/// `vkCmdDrawMeshTasksIndirectCountEXT` routing through the same
/// prepared-draw code `vkCmdDraw*` already uses, mirroring
/// `GraphicsPipelineTest.cpp`'s own `MeshSource`.
constexpr llvm::StringLiteral MeshSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [MeshShadingEXT], [SPV_EXT_mesh_shader]> {
  spirv.func @main() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "MeshEXT" @main
  spirv.ExecutionMode @main "OutputTrianglesEXT"
  spirv.ExecutionMode @main "OutputVertices", 3
  spirv.ExecutionMode @main "OutputPrimitivesEXT", 1
  spirv.ExecutionMode @main "LocalSize", 1, 1, 1
}
)mlir";


/// Roadmap H3a: reads `gl_ViewportIndex` back as a *fragment*-shader
/// `Input`-storage-class builtin (the other half of
/// `GL_ARB_shader_viewport_layer_array`'s support, `dEQP-VK.draw.*.
/// shader_viewport_index.fragment_shader_*`'s own real shader shape:
/// `out_color = color[gl_ViewportIndex]`, simplified here to a two-way
/// select instead of an indexed uniform-block read) -- red for viewport 0,
/// blue for viewport 1. Pairs with `FullscreenVertexWithInstanceViewportSource`
/// (above), which *writes* `gl_ViewportIndex` from the vertex stage.
constexpr llvm::StringLiteral ViewportIndexFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, ShaderViewportIndexLayerEXT], [SPV_EXT_shader_viewport_index_layer]> {
  spirv.GlobalVariable @vp built_in("ViewportIndex") : !spirv.ptr<i32, Input>
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %vpp = spirv.mlir.addressof @vp : !spirv.ptr<i32, Input>
    %vp = spirv.Load "Input" %vpp : i32
    %c1 = spirv.Constant 1 : i32
    %is1 = spirv.IEqual %vp, %c1 : i32
    %red = spirv.Constant dense<[1.0, 0.0, 0.0, 1.0]> : vector<4xf32>
    %blue = spirv.Constant dense<[0.0, 0.0, 1.0, 1.0]> : vector<4xf32>
    %c = spirv.Select %is1, %blue, %red : i1, vector<4xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p, %c : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @vp, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir";


/// A 2-vertex horizontal line at NDC y = -0.25 (screen row 1's pixel
/// center on a 4x4 target -- real Vulkan clip-space Y-down convention,
/// `((row + 0.5) / height) * 2 - 1`, matching `ExecutorTest.
/// RendersAHorizontalLineList`'s own row 1 line once `feme-graphics-
/// canonicalize-stage` negates this SPIR-V-sourced `gl_Position.y`, see
/// roadmap H2g), spanning the full NDC x range: vertex 0 at (-1, -0.25),
/// vertex 1 at (1, -0.25).
constexpr llvm::StringLiteral LineVertexSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @vid built_in("VertexIndex") : !spirv.ptr<i32, Input>
  spirv.GlobalVariable @pos built_in("Position") : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %vidp = spirv.mlir.addressof @vid : !spirv.ptr<i32, Input>
    %v = spirv.Load "Input" %vidp : i32
    %c0 = spirv.Constant 0 : i32
    %is0 = spirv.IEqual %v, %c0 : i32
    %negone = spirv.Constant -1.0 : f32
    %posone = spirv.Constant 1.0 : f32
    %x = spirv.Select %is0, %negone, %posone : i1, f32
    %y = spirv.Constant -0.25 : f32
    %z = spirv.Constant 0.0 : f32
    %w = spirv.Constant 1.0 : f32
    %p = spirv.CompositeConstruct %x, %y, %z, %w : (f32, f32, f32, f32) -> vector<4xf32>
    %posp = spirv.mlir.addressof @pos : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %posp, %p : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @main, @vid, @pos
}
)mlir";

/// Solid green into SV_Target0.
constexpr llvm::StringLiteral GreenFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %c = spirv.Constant dense<[0.0, 1.0, 0.0, 1.0]> : vector<4xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p, %c : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir";

/// (roadmap H7t) Solid green into a 3-component `SV_Target0` -- no alpha
/// channel written at all. Legal per spec, the missing alpha reads back as
/// its identity value (`1.0`, fully opaque).
constexpr llvm::StringLiteral Vec3GreenFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<3xf32>, Output>
  spirv.func @main() -> () "None" {
    %c = spirv.Constant dense<[0.0, 1.0, 0.0]> : vector<3xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<3xf32>, Output>
    spirv.Store "Output" %p, %c : vector<3xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir";

/// Half-alpha red into SV_Target0, for `BlendState::BlendEnable` coverage.
constexpr llvm::StringLiteral HalfAlphaRedFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %c = spirv.Constant dense<[1.0, 0.0, 0.0, 0.5]> : vector<4xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p, %c : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir";

/// Solid red into SV_Target0 and solid green into SV_Target1, for
/// multiple-render-target coverage.
constexpr llvm::StringLiteral DualOutputFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @color0 {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.GlobalVariable @color1 {location = 1 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %c0 = spirv.Constant dense<[1.0, 0.0, 0.0, 1.0]> : vector<4xf32>
    %c1 = spirv.Constant dense<[0.0, 1.0, 0.0, 1.0]> : vector<4xf32>
    %p0 = spirv.mlir.addressof @color0 : !spirv.ptr<vector<4xf32>, Output>
    %p1 = spirv.mlir.addressof @color1 : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p0, %c0 : vector<4xf32>
    spirv.Store "Output" %p1, %c1 : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @color0, @color1
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir";

/// (Roadmap F8a) Reads `input_attachment_index = 0`'s currently-bound
/// color attachment via `subpassLoad` (`Dim::SubpassData` `spirv.ImageRead`,
/// coordinate `(0, 0)` -- "relative to the current fragment location") and
/// writes its own red channel into green, zeroing red/blue and leaving
/// alpha opaque: fed the solid-red attachment `RedFragmentSource` leaves
/// behind, this turns it solid green in place.
constexpr llvm::StringLiteral SubpassLoadFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, InputAttachment], []> {
  spirv.GlobalVariable @in_color bind(0, 0) {input_attachment_index = 0 : i32} : !spirv.ptr<!spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %inp = spirv.mlir.addressof @in_color : !spirv.ptr<!spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, UniformConstant>
    %img = spirv.Load "UniformConstant" %inp : !spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>
    %coord = spirv.Constant dense<0> : vector<2xi32>
    %texel = spirv.ImageRead %img, %coord : !spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, vector<2xi32> -> vector<4xf32>
    %r = spirv.CompositeExtract %texel[0 : i32] : vector<4xf32>
    %zero = spirv.Constant 0.0 : f32
    %one = spirv.Constant 1.0 : f32
    %out = spirv.CompositeConstruct %zero, %r, %zero, %one : (f32, f32, f32, f32) -> vector<4xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p, %out : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @in_color, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir";

/// (Roadmap F8b) The depth-attachment counterpart of
/// `SubpassLoadFragmentSource`: same shape, but the subpass image carries
/// `IsDepth` and its single component is whatever `D16_UNORM`/`D32_FLOAT`
/// decoded, not a color channel.
constexpr llvm::StringLiteral SubpassLoadDepthFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, InputAttachment], []> {
  spirv.GlobalVariable @in_depth bind(0, 0) {input_attachment_index = 0 : i32} : !spirv.ptr<!spirv.image<f32, SubpassData, IsDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %inp = spirv.mlir.addressof @in_depth : !spirv.ptr<!spirv.image<f32, SubpassData, IsDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, UniformConstant>
    %img = spirv.Load "UniformConstant" %inp : !spirv.image<f32, SubpassData, IsDepth, NonArrayed, SingleSampled, NoSampler, Unknown>
    %coord = spirv.Constant dense<0> : vector<2xi32>
    %texel = spirv.ImageRead %img, %coord : !spirv.image<f32, SubpassData, IsDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, vector<2xi32> -> vector<4xf32>
    %r = spirv.CompositeExtract %texel[0 : i32] : vector<4xf32>
    %zero = spirv.Constant 0.0 : f32
    %one = spirv.Constant 1.0 : f32
    %out = spirv.CompositeConstruct %zero, %r, %zero, %one : (f32, f32, f32, f32) -> vector<4xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p, %out : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @in_depth, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir";

/// (Roadmap F8b) The stencil-attachment counterpart of
/// `SubpassLoadFragmentSource`: same shape, reading `S8_UINT`'s single
/// (normalized) component back instead of a color channel.
constexpr llvm::StringLiteral SubpassLoadStencilFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, InputAttachment], []> {
  spirv.GlobalVariable @in_stencil bind(0, 0) {input_attachment_index = 0 : i32} : !spirv.ptr<!spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %inp = spirv.mlir.addressof @in_stencil : !spirv.ptr<!spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, UniformConstant>
    %img = spirv.Load "UniformConstant" %inp : !spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>
    %coord = spirv.Constant dense<0> : vector<2xi32>
    %texel = spirv.ImageRead %img, %coord : !spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, vector<2xi32> -> vector<4xf32>
    %r = spirv.CompositeExtract %texel[0 : i32] : vector<4xf32>
    %zero = spirv.Constant 0.0 : f32
    %one = spirv.Constant 1.0 : f32
    %out = spirv.CompositeConstruct %zero, %r, %zero, %one : (f32, f32, f32, f32) -> vector<4xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p, %out : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @in_stencil, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir";

/// (Roadmap F8c) The explicit-sample counterpart of
/// `SubpassLoadFragmentSource`: the subpass image is `MultiSampled` and the
/// `spirv.ImageRead` carries a lone `Sample` image operand (constant `2`)
/// instead of an implicit sample 0, reading a specific sample of a
/// multisampled color attachment back and writing its red channel into
/// green -- proving `feme::StageOpKind::SubpassLoad`'s new `sample` operand
/// (roadmap F8c) actually selects a real, non-zero sample rather than
/// always reading sample 0.
constexpr llvm::StringLiteral SubpassLoadMultisampleFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, InputAttachment], []> {
  spirv.GlobalVariable @in_color_ms bind(0, 0) {input_attachment_index = 0 : i32} : !spirv.ptr<!spirv.image<f32, SubpassData, NoDepth, NonArrayed, MultiSampled, NoSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %inp = spirv.mlir.addressof @in_color_ms : !spirv.ptr<!spirv.image<f32, SubpassData, NoDepth, NonArrayed, MultiSampled, NoSampler, Unknown>, UniformConstant>
    %img = spirv.Load "UniformConstant" %inp : !spirv.image<f32, SubpassData, NoDepth, NonArrayed, MultiSampled, NoSampler, Unknown>
    %coord = spirv.Constant dense<0> : vector<2xi32>
    %sample = spirv.Constant 2 : si32
    %texel = spirv.ImageRead %img, %coord ["Sample"], %sample : !spirv.image<f32, SubpassData, NoDepth, NonArrayed, MultiSampled, NoSampler, Unknown>, vector<2xi32>, si32 -> vector<4xf32>
    %r = spirv.CompositeExtract %texel[0 : i32] : vector<4xf32>
    %zero = spirv.Constant 0.0 : f32
    %one = spirv.Constant 1.0 : f32
    %out = spirv.CompositeConstruct %zero, %r, %zero, %one : (f32, f32, f32, f32) -> vector<4xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p, %out : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @in_color_ms, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir";

/// Opaque white into `SV_Target0`'s ordinary (`Index=0`) output and
/// (0.25, 0.5, 0.75, 1.0) into its `Index=1` companion at the same
/// `Location=0` -- roadmap C4's dual-source blend coverage
/// (`VK_BLEND_FACTOR_SRC1_*`).
constexpr llvm::StringLiteral DualSourceFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @color0 {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.GlobalVariable @color1 {location = 0 : i32, index = 1 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %c0 = spirv.Constant dense<[1.0, 1.0, 1.0, 1.0]> : vector<4xf32>
    %c1 = spirv.Constant dense<[0.25, 0.5, 0.75, 1.0]> : vector<4xf32>
    %p0 = spirv.mlir.addressof @color0 : !spirv.ptr<vector<4xf32>, Output>
    %p1 = spirv.mlir.addressof @color1 : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p0, %c0 : vector<4xf32>
    spirv.Store "Output" %p1, %c1 : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @color0, @color1
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir";

/// One oversized counter-clockwise triangle at a fixed depth of 0.2 (nearer
/// to the viewer under `CompareOp::Less`), for `DepthState` coverage.
constexpr llvm::StringLiteral NearDepthVertexSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @vid built_in("VertexIndex") : !spirv.ptr<i32, Input>
  spirv.GlobalVariable @pos built_in("Position") : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %vidp = spirv.mlir.addressof @vid : !spirv.ptr<i32, Input>
    %v = spirv.Load "Input" %vidp : i32
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %is0 = spirv.IEqual %v, %c0 : i32
    %is1 = spirv.IEqual %v, %c1 : i32
    %neg1 = spirv.Constant -1.0 : f32
    %three = spirv.Constant 3.0 : f32
    %xb = spirv.Select %is1, %three, %neg1 : i1, f32
    %x = spirv.Select %is0, %neg1, %xb : i1, f32
    %yb = spirv.Select %is1, %neg1, %three : i1, f32
    %y = spirv.Select %is0, %neg1, %yb : i1, f32
    %z = spirv.Constant 0.2 : f32
    %w = spirv.Constant 1.0 : f32
    %p = spirv.CompositeConstruct %x, %y, %z, %w : (f32, f32, f32, f32) -> vector<4xf32>
    %posp = spirv.mlir.addressof @pos : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %posp, %p : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @main, @vid, @pos
}
)mlir";

/// The same oversized triangle, at a fixed depth of 0.8 (farther from the
/// viewer under `CompareOp::Less`).
constexpr llvm::StringLiteral FarDepthVertexSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @vid built_in("VertexIndex") : !spirv.ptr<i32, Input>
  spirv.GlobalVariable @pos built_in("Position") : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %vidp = spirv.mlir.addressof @vid : !spirv.ptr<i32, Input>
    %v = spirv.Load "Input" %vidp : i32
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %is0 = spirv.IEqual %v, %c0 : i32
    %is1 = spirv.IEqual %v, %c1 : i32
    %neg1 = spirv.Constant -1.0 : f32
    %three = spirv.Constant 3.0 : f32
    %xb = spirv.Select %is1, %three, %neg1 : i1, f32
    %x = spirv.Select %is0, %neg1, %xb : i1, f32
    %yb = spirv.Select %is1, %neg1, %three : i1, f32
    %y = spirv.Select %is0, %neg1, %yb : i1, f32
    %z = spirv.Constant 0.8 : f32
    %w = spirv.Constant 1.0 : f32
    %p = spirv.CompositeConstruct %x, %y, %z, %w : (f32, f32, f32, f32) -> vector<4xf32>
    %posp = spirv.mlir.addressof @pos : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %posp, %p : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @main, @vid, @pos
}
)mlir";

/// (roadmap H7d) The same oversized fullscreen triangle, at a fixed clip-
/// space Z of 2.0 (with `w = 1.0`, an NDC Z of 2.0: beyond the far plane's
/// `Z <= W` limit, so this triangle is entirely clipped away when depth
/// clamp is disabled, and entirely visible -- clamped to the viewport's
/// `maxDepth` -- when it is enabled).
constexpr llvm::StringLiteral FarBeyondDepthVertexSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @vid built_in("VertexIndex") : !spirv.ptr<i32, Input>
  spirv.GlobalVariable @pos built_in("Position") : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %vidp = spirv.mlir.addressof @vid : !spirv.ptr<i32, Input>
    %v = spirv.Load "Input" %vidp : i32
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %is0 = spirv.IEqual %v, %c0 : i32
    %is1 = spirv.IEqual %v, %c1 : i32
    %neg1 = spirv.Constant -1.0 : f32
    %three = spirv.Constant 3.0 : f32
    %xb = spirv.Select %is1, %three, %neg1 : i1, f32
    %x = spirv.Select %is0, %neg1, %xb : i1, f32
    %yb = spirv.Select %is1, %neg1, %three : i1, f32
    %y = spirv.Select %is0, %neg1, %yb : i1, f32
    %z = spirv.Constant 2.0 : f32
    %w = spirv.Constant 1.0 : f32
    %p = spirv.CompositeConstruct %x, %y, %z, %w : (f32, f32, f32, f32) -> vector<4xf32>
    %posp = spirv.mlir.addressof @pos : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %posp, %p : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @main, @vid, @pos
}
)mlir";

/// (roadmap H7d) A single triangle whose 3 vertices carry 3 distinct
/// clip-space Z values spanning the whole depth range and beyond it on
/// both ends: vertex 0 at Z=-3.0 (below the near plane), vertex 1 at
/// Z=5.0 (beyond the far plane), vertex 2 at Z=0.5 (in range). Paired
/// with `FragCoordZFragmentSource`, this reproduces the exact class of
/// bug `dEQP-VK.clipping.clip_volume.depth_clamp.*` found: clamping each
/// vertex's own depth *before* interpolating (instead of clamping the
/// *interpolated* depth) produces a false linear ramp between the two
/// out-of-range vertices' clamped endpoints (0.0 and 1.0) instead of the
/// correct, non-monotonic clamped curve.
constexpr llvm::StringLiteral MixedDepthVertexSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @vid built_in("VertexIndex") : !spirv.ptr<i32, Input>
  spirv.GlobalVariable @pos built_in("Position") : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %vidp = spirv.mlir.addressof @vid : !spirv.ptr<i32, Input>
    %v = spirv.Load "Input" %vidp : i32
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %is0 = spirv.IEqual %v, %c0 : i32
    %is1 = spirv.IEqual %v, %c1 : i32
    %neg1 = spirv.Constant -1.0 : f32
    %three = spirv.Constant 3.0 : f32
    %xb = spirv.Select %is1, %three, %neg1 : i1, f32
    %x = spirv.Select %is0, %neg1, %xb : i1, f32
    %yb = spirv.Select %is1, %neg1, %three : i1, f32
    %y = spirv.Select %is0, %neg1, %yb : i1, f32
    %zNeg3 = spirv.Constant -3.0 : f32
    %zPos5 = spirv.Constant 5.0 : f32
    %zHalf = spirv.Constant 0.5 : f32
    %zb = spirv.Select %is1, %zPos5, %zHalf : i1, f32
    %z = spirv.Select %is0, %zNeg3, %zb : i1, f32
    %w = spirv.Constant 1.0 : f32
    %p = spirv.CompositeConstruct %x, %y, %z, %w : (f32, f32, f32, f32) -> vector<4xf32>
    %posp = spirv.mlir.addressof @pos : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %posp, %p : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @main, @vid, @pos
}
)mlir";

/// (roadmap H7d) Writes `gl_FragCoord.z` (the post-viewport-mapping,
/// post-depth-clamp interpolated depth) into the color attachment's own R
/// channel, exactly mirroring `dEQP-VK.clipping.clip_volume.depth_clamp.*`'s
/// own fragment shader (`vec4(1.0, gl_FragCoord.z, 0.0, 1.0)`, modulo which
/// channel carries the depth value) -- also exercises the
/// `loadFragmentSystemValue`/`Position` component-resolution fix
/// (`FragmentWrapper.cpp`), since `.z` is not the first (`.x`) component.
constexpr llvm::StringLiteral FragCoordZFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @fragCoord built_in("FragCoord") : !spirv.ptr<vector<4xf32>, Input>
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %fcp = spirv.mlir.addressof @fragCoord : !spirv.ptr<vector<4xf32>, Input>
    %fc = spirv.Load "Input" %fcp : vector<4xf32>
    %z = spirv.CompositeExtract %fc[2 : i32] : vector<4xf32>
    %zero = spirv.Constant 0.0 : f32
    %one = spirv.Constant 1.0 : f32
    %c = spirv.CompositeConstruct %z, %zero, %zero, %one : (f32, f32, f32, f32) -> vector<4xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p, %c : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @fragCoord, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir";

/// A vertex stage whose position comes from `gl_VertexIndex` (the same
/// oversized fullscreen triangle as `FullscreenVertexSource`) but whose
/// fragment color is a per-instance vertex input attribute at location 1,
/// passed through unchanged -- covers per-instance vertex input rate
/// (`VK_VERTEX_INPUT_RATE_INSTANCE`).
constexpr llvm::StringLiteral PerInstanceColorVertexSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @vid built_in("VertexIndex") : !spirv.ptr<i32, Input>
  spirv.GlobalVariable @aColor {location = 1 : i32} : !spirv.ptr<vector<4xf32>, Input>
  spirv.GlobalVariable @vColor {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.GlobalVariable @pos built_in("Position") : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %vidp = spirv.mlir.addressof @vid : !spirv.ptr<i32, Input>
    %v = spirv.Load "Input" %vidp : i32
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %is0 = spirv.IEqual %v, %c0 : i32
    %is1 = spirv.IEqual %v, %c1 : i32
    %neg1 = spirv.Constant -1.0 : f32
    %three = spirv.Constant 3.0 : f32
    %xb = spirv.Select %is1, %three, %neg1 : i1, f32
    %x = spirv.Select %is0, %neg1, %xb : i1, f32
    %yb = spirv.Select %is1, %neg1, %three : i1, f32
    %y = spirv.Select %is0, %neg1, %yb : i1, f32
    %z = spirv.Constant 0.0 : f32
    %w = spirv.Constant 1.0 : f32
    %p = spirv.CompositeConstruct %x, %y, %z, %w : (f32, f32, f32, f32) -> vector<4xf32>
    %posp = spirv.mlir.addressof @pos : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %posp, %p : vector<4xf32>

    %colp = spirv.mlir.addressof @aColor : !spirv.ptr<vector<4xf32>, Input>
    %col = spirv.Load "Input" %colp : vector<4xf32>
    %vcp = spirv.mlir.addressof @vColor : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %vcp, %col : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @main, @vid, @aColor, @vColor, @pos
}
)mlir";

/// A fragment stage passing its location-0 input straight through to
/// SV_Target0, for `PerInstanceColorVertexSource`'s varying.
constexpr llvm::StringLiteral PassthroughColorFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @vColor {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Input>
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %vp = spirv.mlir.addressof @vColor : !spirv.ptr<vector<4xf32>, Input>
    %v = spirv.Load "Input" %vp : vector<4xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p, %v : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @vColor, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir";

class DrawTest : public ::testing::Test {
protected:
  static constexpr uint32_t Extent = 4;

  void SetUp() override {
    VkInstanceCreateInfo InstInfo{};
    ASSERT_EQ(vkCreateInstance(&InstInfo, nullptr, &Instance), VK_SUCCESS);
    uint32_t Count = 1;
    ASSERT_EQ(vkEnumeratePhysicalDevices(Instance, &Count, &Physical),
              VK_SUCCESS);
    VkDeviceCreateInfo DevInfo{};
    ASSERT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Device), VK_SUCCESS);
    vkGetDeviceQueue(Device, 0, 0, &Queue);

    createColorTarget();
    createRenderPassAndFramebuffer();

    VkPipelineLayoutCreateInfo LayoutInfo{};
    ASSERT_EQ(vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &Layout),
              VK_SUCCESS);

    VkCommandPoolCreateInfo PoolInfo{};
    ASSERT_EQ(vkCreateCommandPool(Device, &PoolInfo, nullptr, &Pool),
              VK_SUCCESS);
    VkCommandBufferAllocateInfo AllocInfo{};
    AllocInfo.commandPool = Pool;
    AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    AllocInfo.commandBufferCount = 1;
    ASSERT_EQ(vkAllocateCommandBuffers(Device, &AllocInfo, &Cmd), VK_SUCCESS);
  }

  void TearDown() override {
    vkDestroyCommandPool(Device, Pool, nullptr);
    vkDestroyPipelineLayout(Device, Layout, nullptr);
    vkDestroyFramebuffer(Device, Framebuffer, nullptr);
    vkDestroyRenderPass(Device, Pass, nullptr);
    vkDestroyImageView(Device, ColorView, nullptr);
    vkDestroyImage(Device, ColorImage, nullptr);
    vkFreeMemory(Device, ColorMemory, nullptr);
    vkDestroyDevice(Device, nullptr);
    vkDestroyInstance(Instance, nullptr);
  }

  void createColorTarget() {
    VkImageCreateInfo ImageInfo{};
    ImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    ImageInfo.extent = {Extent, Extent, 1};
    ImageInfo.mipLevels = 1;
    ImageInfo.arrayLayers = 1;
    ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &ColorImage),
              VK_SUCCESS);
    VkMemoryRequirements Reqs{};
    vkGetImageMemoryRequirements(Device, ColorImage, &Reqs);
    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.allocationSize = Reqs.size;
    ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &ColorMemory),
              VK_SUCCESS);
    ASSERT_EQ(vkBindImageMemory(Device, ColorImage, ColorMemory, 0),
              VK_SUCCESS);

    VkImageViewCreateInfo ViewInfo{};
    ViewInfo.image = ColorImage;
    ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewInfo.subresourceRange.levelCount = 1;
    ViewInfo.subresourceRange.layerCount = 1;
    ASSERT_EQ(vkCreateImageView(Device, &ViewInfo, nullptr, &ColorView),
              VK_SUCCESS);
  }

  void createRenderPassAndFramebuffer() {
    VkAttachmentDescription Attachment{};
    Attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    Attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    Attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription Subpass{};
    Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    Subpass.colorAttachmentCount = 1;
    Subpass.pColorAttachments = &ColorRef;
    VkRenderPassCreateInfo PassInfo{};
    PassInfo.attachmentCount = 1;
    PassInfo.pAttachments = &Attachment;
    PassInfo.subpassCount = 1;
    PassInfo.pSubpasses = &Subpass;
    ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &Pass),
              VK_SUCCESS);

    VkFramebufferCreateInfo FbInfo{};
    FbInfo.renderPass = Pass;
    FbInfo.attachmentCount = 1;
    FbInfo.pAttachments = &ColorView;
    FbInfo.width = Extent;
    FbInfo.height = Extent;
    FbInfo.layers = 1;
    ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &Framebuffer),
              VK_SUCCESS);
  }

  VkShaderModule createModule(llvm::StringRef Source) {
    std::vector<uint32_t> Words = assembleSPIRV(Source);
    EXPECT_FALSE(Words.empty());
    VkShaderModuleCreateInfo Info{};
    Info.codeSize = Words.size() * sizeof(uint32_t);
    Info.pCode = Words.data();
    VkShaderModule Module = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateShaderModule(Device, &Info, nullptr, &Module),
              VK_SUCCESS);
    return Module;
  }

  /// Creates and binds a \p Size-byte buffer with \p Usage, returning its
  /// handle and (via \p OutMemory) its backing allocation.
  VkBuffer createBuffer(VkDeviceSize Size, VkDeviceMemory &OutMemory,
                        VkBufferUsageFlags Usage) {
    VkBufferCreateInfo Info{};
    Info.size = Size;
    Info.usage = Usage;
    VkBuffer Buf = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateBuffer(Device, &Info, nullptr, &Buf), VK_SUCCESS);
    VkMemoryRequirements Reqs{};
    vkGetBufferMemoryRequirements(Device, Buf, &Reqs);
    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.allocationSize = Reqs.size;
    EXPECT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &OutMemory),
              VK_SUCCESS);
    EXPECT_EQ(vkBindBufferMemory(Device, Buf, OutMemory, 0), VK_SUCCESS);
    return Buf;
  }

  /// \p Rendering, when non-null, replaces the fixture's `VkRenderPass`
  /// with a chained `VkPipelineRenderingCreateInfo` (dynamic rendering).
  VkPipeline
  createPipeline(VkShaderModule Vertex, VkShaderModule Fragment,
                 const VkPipelineRenderingCreateInfo *Rendering = nullptr) {
    VkPipelineShaderStageCreateInfo Stages[2]{};
    Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    Stages[0].module = Vertex;
    Stages[0].pName = "main";
    Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    Stages[1].module = Fragment;
    Stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo VertexInput{};
    VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
    InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
    VkRect2D Scissor{{0, 0}, {Extent, Extent}};
    VkPipelineViewportStateCreateInfo ViewportState{};
    ViewportState.viewportCount = 1;
    ViewportState.pViewports = &Viewport;
    ViewportState.scissorCount = 1;
    ViewportState.pScissors = &Scissor;
    VkPipelineRasterizationStateCreateInfo Raster{};
    Raster.cullMode = VK_CULL_MODE_NONE;
    Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Raster.polygonMode = VK_POLYGON_MODE_FILL;
    VkPipelineMultisampleStateCreateInfo Multisample{};
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState BlendAttachment{};
    BlendAttachment.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo Blend{};
    Blend.attachmentCount = 1;
    Blend.pAttachments = &BlendAttachment;

    VkGraphicsPipelineCreateInfo Info{};
    Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    Info.stageCount = 2;
    Info.pStages = Stages;
    Info.pVertexInputState = &VertexInput;
    Info.pInputAssemblyState = &InputAssembly;
    Info.pViewportState = &ViewportState;
    Info.pRasterizationState = &Raster;
    Info.pMultisampleState = &Multisample;
    Info.pColorBlendState = &Blend;
    Info.layout = Layout;
    if (Rendering)
      Info.pNext = Rendering;
    else
      Info.renderPass = Pass;

    VkPipeline Pipe = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info,
                                        nullptr, &Pipe),
              VK_SUCCESS);
    return Pipe;
  }

  /// (roadmap H6f) A mesh pipeline (task stage optional, mesh stage
  /// required, no vertex-input/input-assembly state), mirroring
  /// `createPipeline` above but for `VK_SHADER_STAGE_MESH_BIT_EXT`/
  /// `_TASK_BIT_EXT`.
  VkPipeline createMeshPipeline(VkShaderModule Mesh, VkShaderModule Fragment,
                                VkShaderModule Task = VK_NULL_HANDLE) {
    VkPipelineShaderStageCreateInfo Stages[3]{};
    uint32_t StageCount = 0;
    if (Task) {
      Stages[StageCount].sType =
          VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      Stages[StageCount].stage = VK_SHADER_STAGE_TASK_BIT_EXT;
      Stages[StageCount].module = Task;
      Stages[StageCount].pName = "main";
      ++StageCount;
    }
    Stages[StageCount].sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[StageCount].stage = VK_SHADER_STAGE_MESH_BIT_EXT;
    Stages[StageCount].module = Mesh;
    Stages[StageCount].pName = "main";
    ++StageCount;
    Stages[StageCount].sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[StageCount].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    Stages[StageCount].module = Fragment;
    Stages[StageCount].pName = "main";
    ++StageCount;

    VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
    VkRect2D Scissor{{0, 0}, {Extent, Extent}};
    VkPipelineViewportStateCreateInfo ViewportState{};
    ViewportState.viewportCount = 1;
    ViewportState.pViewports = &Viewport;
    ViewportState.scissorCount = 1;
    ViewportState.pScissors = &Scissor;
    VkPipelineRasterizationStateCreateInfo Raster{};
    Raster.cullMode = VK_CULL_MODE_NONE;
    Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Raster.polygonMode = VK_POLYGON_MODE_FILL;
    VkPipelineMultisampleStateCreateInfo Multisample{};
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState BlendAttachment{};
    BlendAttachment.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo Blend{};
    Blend.attachmentCount = 1;
    Blend.pAttachments = &BlendAttachment;

    VkGraphicsPipelineCreateInfo Info{};
    Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    Info.stageCount = StageCount;
    Info.pStages = Stages;
    Info.pViewportState = &ViewportState;
    Info.pRasterizationState = &Raster;
    Info.pMultisampleState = &Multisample;
    Info.pColorBlendState = &Blend;
    Info.layout = Layout;
    Info.renderPass = Pass;

    VkPipeline Pipe = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info,
                                        nullptr, &Pipe),
              VK_SUCCESS);
    return Pipe;
  }

  void beginRenderPass(VkClearColorValue Clear) {
    VkCommandBufferBeginInfo BeginInfo{};
    ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
    VkClearValue ClearValue{};
    ClearValue.color = Clear;
    VkRenderPassBeginInfo PassBegin{};
    PassBegin.renderPass = Pass;
    PassBegin.framebuffer = Framebuffer;
    PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
    PassBegin.clearValueCount = 1;
    PassBegin.pClearValues = &ClearValue;
    vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  }

  VkResult submit() {
    VkSubmitInfo Submit{};
    Submit.commandBufferCount = 1;
    Submit.pCommandBuffers = &Cmd;
    return vkQueueSubmit(Queue, 1, &Submit, VK_NULL_HANDLE);
  }

  /// Texel (X, Y) of \p Img, as four bytes -- any 4-byte-per-texel color
  /// target, not only the fixture's default one.
  std::array<uint8_t, 4> texelOf(VkImage Img, uint32_t X, uint32_t Y) {
    const auto *Data =
        static_cast<const uint8_t *>(fromHandle<Image>(Img)->data());
    std::array<uint8_t, 4> Result{};
    std::memcpy(Result.data(), Data + ((size_t)Y * Extent + X) * 4, 4);
    return Result;
  }

  /// Texel (X, Y) of the fixture's default color target, as four bytes.
  std::array<uint8_t, 4> texel(uint32_t X, uint32_t Y) {
    return texelOf(ColorImage, X, Y);
  }

  /// Creates and binds a `Extent`x`Extent` image (and its view) with
  /// \p Format, \p Usage, \p Aspect, and \p Samples -- used by tests needing
  /// an attachment beyond the fixture's single default color target (depth,
  /// stencil, a second color attachment, or a multisample source).
  void
  createImageAndView(VkFormat Format, VkImageUsageFlags Usage,
                     VkImageAspectFlags Aspect, VkImage &OutImage,
                     VkImageView &OutView, VkDeviceMemory &OutMemory,
                     VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT) {
    VkImageCreateInfo ImageInfo{};
    ImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ImageInfo.format = Format;
    ImageInfo.extent = {Extent, Extent, 1};
    ImageInfo.mipLevels = 1;
    ImageInfo.arrayLayers = 1;
    ImageInfo.samples = Samples;
    ImageInfo.usage = Usage;
    ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &OutImage),
              VK_SUCCESS);
    VkMemoryRequirements Reqs{};
    vkGetImageMemoryRequirements(Device, OutImage, &Reqs);
    VkMemoryAllocateInfo AllocInfo{};
    AllocInfo.allocationSize = Reqs.size;
    ASSERT_EQ(vkAllocateMemory(Device, &AllocInfo, nullptr, &OutMemory),
              VK_SUCCESS);
    ASSERT_EQ(vkBindImageMemory(Device, OutImage, OutMemory, 0), VK_SUCCESS);

    VkImageViewCreateInfo ViewInfo{};
    ViewInfo.image = OutImage;
    ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ViewInfo.format = Format;
    ViewInfo.subresourceRange.aspectMask = Aspect;
    ViewInfo.subresourceRange.levelCount = 1;
    ViewInfo.subresourceRange.layerCount = 1;
    ASSERT_EQ(vkCreateImageView(Device, &ViewInfo, nullptr, &OutView),
              VK_SUCCESS);
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
  VkQueue Queue = VK_NULL_HANDLE;
  VkDeviceMemory ColorMemory = VK_NULL_HANDLE;
  VkImage ColorImage = VK_NULL_HANDLE;
  VkImageView ColorView = VK_NULL_HANDLE;
  VkRenderPass Pass = VK_NULL_HANDLE;
  VkFramebuffer Framebuffer = VK_NULL_HANDLE;
  VkPipelineLayout Layout = VK_NULL_HANDLE;
  VkCommandPool Pool = VK_NULL_HANDLE;
  VkCommandBuffer Cmd = VK_NULL_HANDLE;
};

/// V6's own end-to-end scenario: an off-screen render pass whose one draw
/// covers the whole render area with the fragment stage's solid red.
TEST_F(DrawTest, RendersTriangleThroughRenderPass) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel = texel(X, Y);
      EXPECT_EQ(Texel[0], 0xFF) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[1], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[2], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[3], 0xFF) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap H7t) Mirrors `RendersTriangleThroughRenderPass` above, but the
/// fragment stage's `SV_Target0` output is a 3-component `vec3` (no alpha
/// channel written): the real R8G8B8A8_UNORM attachment must still read
/// back fully opaque (missing alpha defaults to `1.0` per spec), and the
/// RGB write must land correctly despite the narrower output.
TEST_F(DrawTest, RendersFragmentOutputNarrowerThan4Components) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(Vec3GreenFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  // Clear to a color that would be mistaken for "correct" if the missing
  // alpha were left as garbage/zero rather than defaulted to opaque.
  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 0.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel = texel(X, Y);
      EXPECT_EQ(Texel[0], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[1], 0xFF) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[2], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[3], 0xFF) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap C6: an imageless framebuffer (`VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT`)
/// defers its attachment view to `vkCmdBeginRenderPass`'s own
/// `VkRenderPassAttachmentBeginInfo` instead of binding one at creation
/// time -- otherwise identical to `RendersTriangleThroughRenderPass` above,
/// confirming the render-target binding built from that deferred view
/// renders exactly the same image a concrete framebuffer would.
TEST_F(DrawTest, RendersThroughImagelessFramebuffer) {
  VkFramebufferAttachmentImageInfo AttachmentImageInfo{};
  AttachmentImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  AttachmentImageInfo.width = Extent;
  AttachmentImageInfo.height = Extent;
  AttachmentImageInfo.layerCount = 1;
  VkFormat ViewFormat = VK_FORMAT_R8G8B8A8_UNORM;
  AttachmentImageInfo.viewFormatCount = 1;
  AttachmentImageInfo.pViewFormats = &ViewFormat;
  VkFramebufferAttachmentsCreateInfo AttachmentsInfo{};
  AttachmentsInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO;
  AttachmentsInfo.attachmentImageInfoCount = 1;
  AttachmentsInfo.pAttachmentImageInfos = &AttachmentImageInfo;

  VkFramebufferCreateInfo FbInfo{};
  FbInfo.pNext = &AttachmentsInfo;
  FbInfo.flags = VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT;
  FbInfo.renderPass = Pass;
  FbInfo.attachmentCount = 1;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer ImagelessFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &ImagelessFb),
            VK_SUCCESS);
  EXPECT_TRUE(
      fromHandle<feme::vulkan::Framebuffer>(ImagelessFb)->isImageless());
  EXPECT_TRUE(fromHandle<feme::vulkan::Framebuffer>(ImagelessFb)
                  ->attachments()
                  .empty());

  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValue{};
  ClearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkRenderPassAttachmentBeginInfo AttachmentBeginInfo{};
  AttachmentBeginInfo.sType =
      VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO;
  AttachmentBeginInfo.attachmentCount = 1;
  AttachmentBeginInfo.pAttachments = &ColorView;
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.pNext = &AttachmentBeginInfo;
  PassBegin.renderPass = Pass;
  PassBegin.framebuffer = ImagelessFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 1;
  PassBegin.pClearValues = &ClearValue;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  EXPECT_EQ(texel(0, 0)[0], 0xFF);
  EXPECT_EQ(texel(0, 0)[1], 0x00);

  vkDestroyFramebuffer(Device, ImagelessFb, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// `VK_ATTACHMENT_LOAD_OP_CLEAR` clears exactly the render area, and a
/// dynamic scissor further restricts what a draw may write -- so a draw
/// covering the whole viewport leaves everything outside the scissor at its
/// cleared value.
TEST_F(DrawTest, DynamicScissorRestrictsTheDraw) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;
  VkDynamicState Dynamic = VK_DYNAMIC_STATE_SCISSOR;
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 1;
  DynamicInfo.pDynamicStates = &Dynamic;

  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.pDynamicState = &DynamicInfo;
  Info.layout = Layout;
  Info.renderPass = Pass;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 1.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  VkRect2D Scissor{{0, 0}, {2, 2}};
  vkCmdSetScissor(Cmd, 0, 1, &Scissor);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  EXPECT_EQ(texel(0, 0)[0], 0xFF);
  EXPECT_EQ(texel(1, 1)[0], 0xFF);
  // Outside the scissor: still the clear color (blue).
  EXPECT_EQ(texel(3, 3)[0], 0x00);
  EXPECT_EQ(texel(3, 3)[2], 0xFF);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap F13) `VK_ATTACHMENT_LOAD_OP_NONE`/`VK_ATTACHMENT_STORE_OP_NONE`
/// (`VK_KHR_load_store_op_none`) both mean "leave the attachment's memory
/// as-is": unlike `VK_ATTACHMENT_LOAD_OP_CLEAR`, `NONE` must not touch
/// whatever the image already held outside of what a draw itself writes,
/// and `STORE_OP_NONE` must not prevent a draw's own writes from landing
/// (this ICD writes straight into the bound image; there is no discard to
/// perform).
TEST_F(DrawTest, LoadStoreOpNoneLeavesUntouchedTexelsAlone) {
  // Sentinel pattern the fixture's default `LOAD_OP_CLEAR` render pass would
  // never produce, so any survivor unambiguously proves `NONE` did not
  // clear.
  auto *Data = static_cast<uint8_t *>(fromHandle<Image>(ColorImage)->data());
  std::memset(Data, 0x7F, size_t(Extent) * Extent * 4);

  VkAttachmentDescription Attachment{};
  Attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  Attachment.loadOp = VK_ATTACHMENT_LOAD_OP_NONE_KHR;
  Attachment.storeOp = VK_ATTACHMENT_STORE_OP_NONE_KHR;
  VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 1;
  Subpass.pColorAttachments = &ColorRef;
  VkRenderPassCreateInfo PassInfo{};
  PassInfo.attachmentCount = 1;
  PassInfo.pAttachments = &Attachment;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass NoneOpsPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &NoneOpsPass),
            VK_SUCCESS);
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = NoneOpsPass;
  FbInfo.attachmentCount = 1;
  FbInfo.pAttachments = &ColorView;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer NoneOpsFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &NoneOpsFb),
            VK_SUCCESS);

  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {2, 2}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;
  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.layout = Layout;
  Info.renderPass = NoneOpsPass;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = NoneOpsPass;
  PassBegin.framebuffer = NoneOpsFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // Inside the scissor: the draw's own red, same as any other load/store op.
  EXPECT_EQ(texel(0, 0)[0], 0xFF);
  EXPECT_EQ(texel(1, 1)[0], 0xFF);
  // Outside the scissor: `LOAD_OP_NONE` left the sentinel alone -- a
  // `LOAD_OP_CLEAR` render pass (every other test in this file) would have
  // zeroed it instead.
  EXPECT_EQ(texel(3, 3)[0], 0x7F);
  EXPECT_EQ(texel(3, 3)[1], 0x7F);
  EXPECT_EQ(texel(3, 3)[2], 0x7F);
  EXPECT_EQ(texel(3, 3)[3], 0x7F);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyFramebuffer(Device, NoneOpsFb, nullptr);
  vkDestroyRenderPass(Device, NoneOpsPass, nullptr);
}

/// (roadmap C4c) `VK_DYNAMIC_STATE_CULL_MODE`: a pipeline that declares it
/// dynamic must actually cull per whatever `vkCmdSetCullModeEXT` last
/// recorded, not per its (irrelevant) creation-time `cullMode`.
TEST_F(DrawTest, DynamicCullModeControlsCulling) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  // Creation-time cull mode is deliberately `FRONT_AND_BACK` (would cull
  // everything) to prove the dynamic value, not this one, governs the draw.
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;
  VkDynamicState Dynamic = VK_DYNAMIC_STATE_CULL_MODE;
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 1;
  DynamicInfo.pDynamicStates = &Dynamic;

  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.pDynamicState = &DynamicInfo;
  Info.layout = Layout;
  Info.renderPass = Pass;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 1.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdSetCullModeEXT(Cmd, VK_CULL_MODE_NONE);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // Cull mode set dynamically to `NONE`: the fullscreen triangle is drawn.
  EXPECT_EQ(texel(2, 2)[0], 0xFF);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap F5) `vkCmdSetLineWidth`, exercised end to end through a real
/// pipeline/command buffer: a pipeline declaring `VK_DYNAMIC_STATE_LINE_
/// WIDTH` draws its line at the width the last `vkCmdSetLineWidth` call
/// set, not its own (deliberately mismatched, 1-pixel) creation-time
/// value.
TEST_F(DrawTest, DynamicLineWidthWidensTheLine) {
  VkShaderModule Vertex = createModule(LineVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  Raster.lineWidth = 1.0f; // deliberately mismatched: see the test comment
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;
  VkDynamicState Dynamic = VK_DYNAMIC_STATE_LINE_WIDTH;
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 1;
  DynamicInfo.pDynamicStates = &Dynamic;

  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.pDynamicState = &DynamicInfo;
  Info.layout = Layout;
  Info.renderPass = Pass;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 0.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdSetLineWidth(Cmd, 3.0f);
  vkCmdDraw(Cmd, 2, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // A 3-pixel-wide line centered on row 1 covers rows 0-2, not just row 1.
  EXPECT_EQ(texel(0, 0)[3], 0xFF);
  EXPECT_EQ(texel(0, 1)[3], 0xFF);
  EXPECT_EQ(texel(0, 2)[3], 0xFF);
  EXPECT_EQ(texel(0, 3)[3], 0);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// An indexed draw fetches its vertices through the bound index buffer.
TEST_F(DrawTest, RendersIndexedDraw) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);

  VkDeviceMemory IndexMemory = VK_NULL_HANDLE;
  VkBuffer IndexBuffer = createBuffer(3 * sizeof(uint32_t), IndexMemory,
                                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
  uint32_t Indices[3] = {0, 1, 2};
  std::memcpy(fromHandle<Buffer>(IndexBuffer)->data(), Indices,
              sizeof(Indices));

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdBindIndexBuffer(Cmd, IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(Cmd, 3, 1, 0, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  EXPECT_EQ(texel(2, 2)[0], 0xFF);
  EXPECT_EQ(texel(2, 2)[3], 0xFF);

  vkDestroyBuffer(Device, IndexBuffer, nullptr);
  vkFreeMemory(Device, IndexMemory, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap F7 (`VK_KHR_index_type_uint8`): the same indexed draw as above,
/// but through an 8-bit index buffer -- `vkCmdBindIndexBuffer`'s own index
/// read (`CommandBuffer.cpp`) and the executor's fetch (`Executor.cpp`)
/// must both honor `VK_INDEX_TYPE_UINT8` exactly like their pre-existing
/// 16-/32-bit cases.
TEST_F(DrawTest, RendersIndexedDrawWithEightBitIndices) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);

  VkDeviceMemory IndexMemory = VK_NULL_HANDLE;
  VkBuffer IndexBuffer = createBuffer(3 * sizeof(uint8_t), IndexMemory,
                                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
  uint8_t Indices[3] = {0, 1, 2};
  std::memcpy(fromHandle<Buffer>(IndexBuffer)->data(), Indices,
              sizeof(Indices));

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdBindIndexBuffer(Cmd, IndexBuffer, 0, VK_INDEX_TYPE_UINT8);
  vkCmdDrawIndexed(Cmd, 3, 1, 0, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  EXPECT_EQ(texel(2, 2)[0], 0xFF);
  EXPECT_EQ(texel(2, 2)[3], 0xFF);

  vkDestroyBuffer(Device, IndexBuffer, nullptr);
  vkFreeMemory(Device, IndexMemory, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// bind as `vkCmdBindIndexBuffer` above when its own `size` covers the
/// whole remaining buffer.
TEST_F(DrawTest, RendersIndexedDrawThroughBindIndexBuffer2) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);

  VkDeviceMemory IndexMemory = VK_NULL_HANDLE;
  VkBuffer IndexBuffer = createBuffer(3 * sizeof(uint32_t), IndexMemory,
                                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
  uint32_t Indices[3] = {0, 1, 2};
  std::memcpy(fromHandle<Buffer>(IndexBuffer)->data(), Indices,
              sizeof(Indices));

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdBindIndexBuffer2(Cmd, IndexBuffer, 0, VK_WHOLE_SIZE,
                        VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(Cmd, 3, 1, 0, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  EXPECT_EQ(texel(2, 2)[0], 0xFF);
  EXPECT_EQ(texel(2, 2)[3], 0xFF);

  vkDestroyBuffer(Device, IndexBuffer, nullptr);
  vkFreeMemory(Device, IndexMemory, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap E5: `vkCmdBindIndexBuffer2`'s `size` bounds the readable index
/// range to less than the whole buffer -- a draw whose index range would
/// have fit the whole buffer, but not the narrower bound `size` gives it,
/// is rejected.
TEST_F(DrawTest, RejectsIndexRangeBeyondBindIndexBuffer2Size) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);

  VkDeviceMemory IndexMemory = VK_NULL_HANDLE;
  VkBuffer IndexBuffer = createBuffer(3 * sizeof(uint32_t), IndexMemory,
                                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
  uint32_t Indices[3] = {0, 1, 2};
  std::memcpy(fromHandle<Buffer>(IndexBuffer)->data(), Indices,
              sizeof(Indices));

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  // Only the first 2 indices are bound; drawing all 3 overruns that bound.
  vkCmdBindIndexBuffer2(Cmd, IndexBuffer, 0, 2 * sizeof(uint32_t),
                        VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(Cmd, 3, 1, 0, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  EXPECT_EQ(submit(), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyBuffer(Device, IndexBuffer, nullptr);
  vkFreeMemory(Device, IndexMemory, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// rather than once per vertex: `firstInstance` selects the buffer's second
/// element (green), not its first (red) -- a per-vertex-rate fetch would
/// instead read vertex index 0 and always see the first element.
TEST_F(DrawTest, RendersPerInstanceVertexAttribute) {
  VkShaderModule Vertex = createModule(PerInstanceColorVertexSource);
  VkShaderModule Fragment = createModule(PassthroughColorFragmentSource);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";

  VkVertexInputBindingDescription BindingDesc{0, sizeof(float) * 4,
                                              VK_VERTEX_INPUT_RATE_INSTANCE};
  VkVertexInputAttributeDescription AttrDesc{1, 0,
                                             VK_FORMAT_R32G32B32A32_SFLOAT, 0};
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VertexInput.vertexBindingDescriptionCount = 1;
  VertexInput.pVertexBindingDescriptions = &BindingDesc;
  VertexInput.vertexAttributeDescriptionCount = 1;
  VertexInput.pVertexAttributeDescriptions = &AttrDesc;
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;

  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.layout = Layout;
  Info.renderPass = Pass;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  VkDeviceMemory InstanceMemory = VK_NULL_HANDLE;
  VkBuffer InstanceBuffer = createBuffer(2 * sizeof(float) * 4, InstanceMemory,
                                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
  float Colors[8] = {
      1.0f, 0.0f, 0.0f, 1.0f, // instance 0: red
      0.0f, 1.0f, 0.0f, 1.0f, // instance 1: green
  };
  std::memcpy(fromHandle<Buffer>(InstanceBuffer)->data(), Colors,
              sizeof(Colors));

  VkDeviceSize Offset = 0;
  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdBindVertexBuffers(Cmd, 0, 1, &InstanceBuffer, &Offset);
  vkCmdDraw(Cmd, 3, 1, 0, 1);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  EXPECT_EQ(texel(2, 2)[0], 0x00);
  EXPECT_EQ(texel(2, 2)[1], 0xFF);
  EXPECT_EQ(texel(2, 2)[2], 0x00);

  vkDestroyBuffer(Device, InstanceBuffer, nullptr);
  vkFreeMemory(Device, InstanceMemory, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap F10) An out-of-bounds vertex attribute fetch must read as zero
/// and let the draw finish, matching `robustBufferAccess`'s unconditional
/// "on" state (`PhysicalDeviceInfo.cpp`) and, once opted into per pipeline,
/// `VkPipelineRobustnessCreateInfo::vertexInputs` -- not fail the whole
/// submit. Only one instance's worth of color data is bound; drawing 2
/// instances makes instance 1's per-instance fetch land entirely past the
/// buffer's end. Since both instances render the same full-screen
/// triangle, the final visible pixel is whichever instance was drawn last
/// (instance 1's all-zero color), exactly like
/// `RendersVertexAttributeInstanceRateDivisor`'s own "last one drawn wins"
/// reasoning below.
TEST_F(DrawTest, OutOfBoundsPerInstanceVertexAttributeReadsZero) {
  VkShaderModule Vertex = createModule(PerInstanceColorVertexSource);
  VkShaderModule Fragment = createModule(PassthroughColorFragmentSource);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";

  VkVertexInputBindingDescription BindingDesc{0, sizeof(float) * 4,
                                              VK_VERTEX_INPUT_RATE_INSTANCE};
  VkVertexInputAttributeDescription AttrDesc{1, 0,
                                             VK_FORMAT_R32G32B32A32_SFLOAT, 0};
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VertexInput.vertexBindingDescriptionCount = 1;
  VertexInput.pVertexBindingDescriptions = &BindingDesc;
  VertexInput.vertexAttributeDescriptionCount = 1;
  VertexInput.pVertexAttributeDescriptions = &AttrDesc;
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;

  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.layout = Layout;
  Info.renderPass = Pass;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  VkDeviceMemory InstanceMemory = VK_NULL_HANDLE;
  VkBuffer InstanceBuffer = createBuffer(sizeof(float) * 4, InstanceMemory,
                                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
  float Colors[4] = {1.0f, 0.0f, 0.0f, 1.0f}; // instance 0: red
  std::memcpy(fromHandle<Buffer>(InstanceBuffer)->data(), Colors,
              sizeof(Colors));

  VkDeviceSize Offset = 0;
  beginRenderPass(VkClearColorValue{{0.0f, 1.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdBindVertexBuffers(Cmd, 0, 1, &InstanceBuffer, &Offset);
  vkCmdDraw(Cmd, 3, 2, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  EXPECT_EQ(texel(2, 2)[0], 0x00);
  EXPECT_EQ(texel(2, 2)[1], 0x00);
  EXPECT_EQ(texel(2, 2)[2], 0x00);
  EXPECT_EQ(texel(2, 2)[3], 0x00);

  vkDestroyBuffer(Device, InstanceBuffer, nullptr);
  vkFreeMemory(Device, InstanceMemory, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap F6) `VK_KHR_vertex_attribute_divisor`: a divisor of 2 makes the
/// per-instance fetch advance once every 2 instances rather than every one,
/// so drawing 4 instances against a 2-element buffer stays in bounds --
/// without the divisor, instance 3's plain per-instance fetch would read
/// past the buffer's end. Every instance renders the same full-screen
/// triangle, so the final visible color is whichever color instance 3 (the
/// last one drawn) fetches: `firstInstance(0) + (3 - 0) / 2 == 1`, the
/// buffer's second (green) element.
TEST_F(DrawTest, RendersVertexAttributeInstanceRateDivisor) {
  VkShaderModule Vertex = createModule(PerInstanceColorVertexSource);
  VkShaderModule Fragment = createModule(PassthroughColorFragmentSource);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";

  VkVertexInputBindingDescription BindingDesc{0, sizeof(float) * 4,
                                              VK_VERTEX_INPUT_RATE_INSTANCE};
  VkVertexInputAttributeDescription AttrDesc{1, 0,
                                             VK_FORMAT_R32G32B32A32_SFLOAT, 0};
  VkVertexInputBindingDivisorDescription DivisorDesc{/*binding=*/0,
                                                     /*divisor=*/2};
  VkPipelineVertexInputDivisorStateCreateInfo DivisorState{};
  DivisorState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO;
  DivisorState.vertexBindingDivisorCount = 1;
  DivisorState.pVertexBindingDivisors = &DivisorDesc;
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VertexInput.pNext = &DivisorState;
  VertexInput.vertexBindingDescriptionCount = 1;
  VertexInput.pVertexBindingDescriptions = &BindingDesc;
  VertexInput.vertexAttributeDescriptionCount = 1;
  VertexInput.pVertexAttributeDescriptions = &AttrDesc;
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;

  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.layout = Layout;
  Info.renderPass = Pass;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  VkDeviceMemory InstanceMemory = VK_NULL_HANDLE;
  VkBuffer InstanceBuffer = createBuffer(2 * sizeof(float) * 4, InstanceMemory,
                                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
  float Colors[8] = {
      1.0f, 0.0f, 0.0f, 1.0f, // element 0: red
      0.0f, 1.0f, 0.0f, 1.0f, // element 1: green
  };
  std::memcpy(fromHandle<Buffer>(InstanceBuffer)->data(), Colors,
              sizeof(Colors));

  VkDeviceSize Offset = 0;
  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdBindVertexBuffers(Cmd, 0, 1, &InstanceBuffer, &Offset);
  vkCmdDraw(Cmd, 3, /*instanceCount=*/4, 0, /*firstInstance=*/0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  EXPECT_EQ(texel(2, 2)[0], 0x00);
  EXPECT_EQ(texel(2, 2)[1], 0xFF);
  EXPECT_EQ(texel(2, 2)[2], 0x00);

  vkDestroyBuffer(Device, InstanceBuffer, nullptr);
  vkFreeMemory(Device, InstanceMemory, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap F6) `vertexAttributeInstanceRateZeroDivisor`: a divisor of 0
/// means every instance reads the vertex at `firstInstance`, regardless of
/// its own instance index -- here `firstInstance == 1` selects the
/// buffer's second (green) element for every one of 3 instances, not just
/// the second.
TEST_F(DrawTest, RendersVertexAttributeInstanceRateZeroDivisor) {
  VkShaderModule Vertex = createModule(PerInstanceColorVertexSource);
  VkShaderModule Fragment = createModule(PassthroughColorFragmentSource);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";

  VkVertexInputBindingDescription BindingDesc{0, sizeof(float) * 4,
                                              VK_VERTEX_INPUT_RATE_INSTANCE};
  VkVertexInputAttributeDescription AttrDesc{1, 0,
                                             VK_FORMAT_R32G32B32A32_SFLOAT, 0};
  VkVertexInputBindingDivisorDescription DivisorDesc{/*binding=*/0,
                                                     /*divisor=*/0};
  VkPipelineVertexInputDivisorStateCreateInfo DivisorState{};
  DivisorState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO;
  DivisorState.vertexBindingDivisorCount = 1;
  DivisorState.pVertexBindingDivisors = &DivisorDesc;
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VertexInput.pNext = &DivisorState;
  VertexInput.vertexBindingDescriptionCount = 1;
  VertexInput.pVertexBindingDescriptions = &BindingDesc;
  VertexInput.vertexAttributeDescriptionCount = 1;
  VertexInput.pVertexAttributeDescriptions = &AttrDesc;
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;

  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.layout = Layout;
  Info.renderPass = Pass;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  VkDeviceMemory InstanceMemory = VK_NULL_HANDLE;
  VkBuffer InstanceBuffer = createBuffer(2 * sizeof(float) * 4, InstanceMemory,
                                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
  float Colors[8] = {
      1.0f, 0.0f, 0.0f, 1.0f, // element 0: red
      0.0f, 1.0f, 0.0f, 1.0f, // element 1: green
  };
  std::memcpy(fromHandle<Buffer>(InstanceBuffer)->data(), Colors,
              sizeof(Colors));

  VkDeviceSize Offset = 0;
  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdBindVertexBuffers(Cmd, 0, 1, &InstanceBuffer, &Offset);
  vkCmdDraw(Cmd, 3, /*instanceCount=*/3, 0, /*firstInstance=*/1);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  EXPECT_EQ(texel(2, 2)[0], 0x00);
  EXPECT_EQ(texel(2, 2)[1], 0xFF);
  EXPECT_EQ(texel(2, 2)[2], 0x00);

  vkDestroyBuffer(Device, InstanceBuffer, nullptr);
  vkFreeMemory(Device, InstanceMemory, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// created with a deliberately wrong static stride (double the real
/// per-instance record size, which would read instance 1's color at half
/// its correct offset) still renders correctly once
/// `vkCmdBindVertexBuffers2EXT`'s `pStrides` overrides it dynamically.
TEST_F(DrawTest, DynamicVertexInputBindingStrideOverridesStaticStride) {
  VkShaderModule Vertex = createModule(PerInstanceColorVertexSource);
  VkShaderModule Fragment = createModule(PassthroughColorFragmentSource);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";

  // Wrong on purpose: the real per-instance record is `sizeof(float) * 4`.
  VkVertexInputBindingDescription BindingDesc{0, sizeof(float) * 8,
                                              VK_VERTEX_INPUT_RATE_INSTANCE};
  VkVertexInputAttributeDescription AttrDesc{1, 0,
                                             VK_FORMAT_R32G32B32A32_SFLOAT, 0};
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VertexInput.vertexBindingDescriptionCount = 1;
  VertexInput.pVertexBindingDescriptions = &BindingDesc;
  VertexInput.vertexAttributeDescriptionCount = 1;
  VertexInput.pVertexAttributeDescriptions = &AttrDesc;
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;
  VkDynamicState Dynamic = VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE;
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 1;
  DynamicInfo.pDynamicStates = &Dynamic;

  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.pDynamicState = &DynamicInfo;
  Info.layout = Layout;
  Info.renderPass = Pass;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  VkDeviceMemory InstanceMemory = VK_NULL_HANDLE;
  VkBuffer InstanceBuffer = createBuffer(2 * sizeof(float) * 4, InstanceMemory,
                                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
  float Colors[8] = {
      1.0f, 0.0f, 0.0f, 1.0f, // instance 0: red
      0.0f, 1.0f, 0.0f, 1.0f, // instance 1: green
  };
  std::memcpy(fromHandle<Buffer>(InstanceBuffer)->data(), Colors,
              sizeof(Colors));

  VkDeviceSize Offset = 0;
  VkDeviceSize Stride = sizeof(float) * 4;
  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdBindVertexBuffers2EXT(Cmd, 0, 1, &InstanceBuffer, &Offset, nullptr,
                             &Stride);
  vkCmdDraw(Cmd, 3, 1, 0, 1);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // The dynamic stride (not the pipeline's wrong static one) picked out
  // instance 1's color: green.
  EXPECT_EQ(texel(2, 2)[0], 0x00);
  EXPECT_EQ(texel(2, 2)[1], 0xFF);
  EXPECT_EQ(texel(2, 2)[2], 0x00);

  vkDestroyBuffer(Device, InstanceBuffer, nullptr);
  vkFreeMemory(Device, InstanceMemory, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// A draw recorded outside a render pass instance, and a draw with no bound
/// graphics pipeline, both fail the submission rather than rendering
/// somewhere undefined.
TEST_F(DrawTest, RejectsDrawOutsideRenderPass) {
  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  EXPECT_EQ(submit(), VK_ERROR_INITIALIZATION_FAILED);
}

/// An indirect draw reads its `VkDrawIndirectCommand` from a bound buffer,
/// validated exactly like an indirect dispatch's group counts.
TEST_F(DrawTest, RendersIndirectDraw) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);

  VkDeviceMemory Memory = VK_NULL_HANDLE;
  VkBuffer Indirect = createBuffer(sizeof(VkDrawIndirectCommand), Memory,
                                   VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
  VkDrawIndirectCommand Args{};
  Args.vertexCount = 3;
  Args.instanceCount = 1;
  std::memcpy(fromHandle<Buffer>(Indirect)->data(), &Args, sizeof(Args));

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDrawIndirect(Cmd, Indirect, 0, 1, sizeof(VkDrawIndirectCommand));
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  EXPECT_EQ(texel(1, 2)[0], 0xFF);
  EXPECT_EQ(texel(1, 2)[3], 0xFF);

  vkDestroyBuffer(Device, Indirect, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// An indirect draw whose command array overruns its buffer is rejected,
/// not clamped -- and so is one whose stride is smaller than the command it
/// describes.
TEST_F(DrawTest, RejectsOutOfBoundsIndirectDraw) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);

  VkDeviceMemory Memory = VK_NULL_HANDLE;
  VkBuffer Indirect = createBuffer(sizeof(VkDrawIndirectCommand), Memory,
                                   VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  // Two commands in a one-command buffer.
  vkCmdDrawIndirect(Cmd, Indirect, 0, 2, sizeof(VkDrawIndirectCommand));
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  EXPECT_EQ(submit(), VK_ERROR_INITIALIZATION_FAILED);

  vkResetCommandBuffer(Cmd, 0);
  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDrawIndirect(Cmd, Indirect, 0, 1, 4);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  EXPECT_EQ(submit(), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyBuffer(Device, Indirect, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap H6f) `vkCmdDrawMeshTasksEXT` routes through the same
/// prepared-draw code (`runPreparedDraw`) `vkCmdDraw` does: it resolves
/// attachments, binds resources, and drives the executor's rasterization
/// tail exactly the same way, just with `PreparedDraw::MeshDraws` set
/// instead of `PreparedDraw::Draws`. Since `MeshSource`'s entry point emits
/// no real mesh output yet (blocked on roadmap H6h/H6i), the render pass's
/// own clear color is left untouched -- the same "correctly wired but
/// produces nothing yet" shape `ExecutorTest.cpp`'s own H6e-era mesh-draw
/// cases already established, now proven reachable from the real
/// `vkCmdDrawMeshTasksEXT` entry point rather than only `Executor.cpp`'s
/// own unit-level `executeDraws` call.
TEST_F(DrawTest, DrawMeshTasksRunsWithoutErrorAndProducesNoOutputYet) {
  VkShaderModule Mesh = createModule(MeshSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createMeshPipeline(Mesh, Fragment);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDrawMeshTasksEXT(Cmd, 1, 1, 1);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  EXPECT_EQ(texel(1, 2)[0], 0x00);
  EXPECT_EQ(texel(1, 2)[3], 0xFF);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Mesh, nullptr);
}

/// A mesh pipeline with a bound task stage dispatches through
/// `vkCmdDrawMeshTasksEXT` too (the task stage's own dispatch shape, per
/// the specification): still no real output since `TaskSource`'s entry
/// point never calls `EmitMeshTasksEXT` (roadmap H6h/H6i).
TEST_F(DrawTest, DrawMeshTasksWithTaskStageRunsWithoutError) {
  constexpr llvm::StringLiteral TaskSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [MeshShadingEXT], [SPV_EXT_mesh_shader]> {
  spirv.func @main() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "TaskEXT" @main
  spirv.ExecutionMode @main "LocalSize", 1, 1, 1
}
)mlir";
  VkShaderModule Mesh = createModule(MeshSource);
  VkShaderModule Task = createModule(TaskSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createMeshPipeline(Mesh, Fragment, Task);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDrawMeshTasksEXT(Cmd, 1, 1, 1);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Task, nullptr);
  vkDestroyShaderModule(Device, Mesh, nullptr);
}

/// `vkCmdDrawMeshTasksIndirectEXT` reads its
/// `VkDrawMeshTasksIndirectCommandEXT` array from a bound buffer, mirroring
/// `RendersIndirectDraw`'s own indirect-vertex-draw coverage.
TEST_F(DrawTest, DrawMeshTasksIndirectReadsBuffer) {
  VkShaderModule Mesh = createModule(MeshSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createMeshPipeline(Mesh, Fragment);

  VkDeviceMemory Memory = VK_NULL_HANDLE;
  VkBuffer Indirect =
      createBuffer(sizeof(VkDrawMeshTasksIndirectCommandEXT), Memory,
                  VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
  VkDrawMeshTasksIndirectCommandEXT Args{1, 1, 1};
  std::memcpy(fromHandle<Buffer>(Indirect)->data(), &Args, sizeof(Args));

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDrawMeshTasksIndirectEXT(Cmd, Indirect, 0, 1,
                               sizeof(VkDrawMeshTasksIndirectCommandEXT));
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  vkDestroyBuffer(Device, Indirect, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Mesh, nullptr);
}

/// A `vkCmdDrawMeshTasksIndirectEXT` whose command array overruns its
/// buffer is rejected, not clamped, mirroring
/// `RejectsOutOfBoundsIndirectDraw`.
TEST_F(DrawTest, RejectsOutOfBoundsIndirectMeshTasksDraw) {
  VkShaderModule Mesh = createModule(MeshSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createMeshPipeline(Mesh, Fragment);

  VkDeviceMemory Memory = VK_NULL_HANDLE;
  VkBuffer Indirect =
      createBuffer(sizeof(VkDrawMeshTasksIndirectCommandEXT), Memory,
                  VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  // Two commands in a one-command buffer.
  vkCmdDrawMeshTasksIndirectEXT(Cmd, Indirect, 0, 2,
                               sizeof(VkDrawMeshTasksIndirectCommandEXT));
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  EXPECT_EQ(submit(), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyBuffer(Device, Indirect, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Mesh, nullptr);
}

/// `vkCmdDrawMeshTasksIndirectCountEXT` clamps its actual draw count to the
/// minimum of `maxDrawCount` and the `uint32_t` stored in its count buffer,
/// per the specification.
TEST_F(DrawTest, DrawMeshTasksIndirectCountClampsToCountBuffer) {
  VkShaderModule Mesh = createModule(MeshSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createMeshPipeline(Mesh, Fragment);

  VkDeviceMemory IndirectMemory = VK_NULL_HANDLE;
  VkBuffer Indirect =
      createBuffer(2 * sizeof(VkDrawMeshTasksIndirectCommandEXT),
                  IndirectMemory, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
  VkDrawMeshTasksIndirectCommandEXT Args[2] = {{1, 1, 1}, {1, 1, 1}};
  std::memcpy(fromHandle<Buffer>(Indirect)->data(), Args, sizeof(Args));

  VkDeviceMemory CountMemory = VK_NULL_HANDLE;
  VkBuffer CountBuffer =
      createBuffer(sizeof(uint32_t), CountMemory,
                  VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
  // The count buffer names only one draw, even though `maxDrawCount` (2)
  // and the indirect buffer itself both have room for two.
  uint32_t Count = 1;
  std::memcpy(fromHandle<Buffer>(CountBuffer)->data(), &Count, sizeof(Count));

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDrawMeshTasksIndirectCountEXT(
      Cmd, Indirect, 0, CountBuffer, 0, 2,
      sizeof(VkDrawMeshTasksIndirectCommandEXT));
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  vkDestroyBuffer(Device, CountBuffer, nullptr);
  vkFreeMemory(Device, CountMemory, nullptr);
  vkDestroyBuffer(Device, Indirect, nullptr);
  vkFreeMemory(Device, IndirectMemory, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Mesh, nullptr);
}

/// `vkCmdDrawMeshTasksEXT` with a non-mesh (vertex) pipeline bound is
/// rejected: mesh dispatch commands require `GraphicsPipeline::
/// hasMeshStages()`, exactly as a vertex `vkCmdDraw` bound to a mesh
/// pipeline would be rejected by `Executor.cpp`'s own mutual-exclusion
/// check the other way around.
TEST_F(DrawTest, DrawMeshTasksRejectsANonMeshPipeline) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDrawMeshTasksEXT(Cmd, 1, 1, 1);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  EXPECT_EQ(submit(), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// `vkCmdDrawMeshTasksEXT` with no bound graphics pipeline at all is
/// rejected, mirroring `RejectsDrawOutsideRenderPass`'s own "well-formed
/// command stream, invalid state" shape.
TEST_F(DrawTest, DrawMeshTasksWithoutBoundPipelineFails) {
  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdDrawMeshTasksEXT(Cmd, 1, 1, 1);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  EXPECT_EQ(submit(), VK_ERROR_INITIALIZATION_FAILED);
}

/// An indexed draw whose index range overruns its bound index buffer is
/// rejected before anything is fetched.
TEST_F(DrawTest, RejectsOutOfBoundsIndexRange) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);

  VkDeviceMemory Memory = VK_NULL_HANDLE;
  VkBuffer IndexBuffer = createBuffer(3 * sizeof(uint32_t), Memory,
                                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdBindIndexBuffer(Cmd, IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(Cmd, 6, 1, 0, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  EXPECT_EQ(submit(), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyBuffer(Device, IndexBuffer, nullptr);
  vkFreeMemory(Device, Memory, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Dynamic rendering reaches the same normalized render-target binding a
/// `VkRenderPass` compiles into: the same shaders, clear and draw produce
/// the same image through `vkCmdBeginRenderingKHR`.
TEST_F(DrawTest, RendersThroughDynamicRendering) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);

  VkFormat ColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 1;
  Rendering.pColorAttachmentFormats = &ColorFormat;
  VkPipeline Pipe = createPipeline(Vertex, Fragment, &Rendering);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);

  VkRenderingAttachmentInfo ColorAttachment{};
  ColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  ColorAttachment.imageView = ColorView;
  ColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  ColorAttachment.clearValue.color = {{0.0f, 1.0f, 0.0f, 1.0f}};

  VkRenderingInfo RenderingInfo{};
  RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  RenderingInfo.renderArea = {{0, 0}, {Extent, Extent}};
  RenderingInfo.layerCount = 1;
  RenderingInfo.colorAttachmentCount = 1;
  RenderingInfo.pColorAttachments = &ColorAttachment;

  vkCmdBeginRenderingKHR(Cmd, &RenderingInfo);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderingKHR(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      EXPECT_EQ(texel(X, Y)[0], 0xFF) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(texel(X, Y)[1], 0x00) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap E5 (`VK_KHR_maintenance5`): a `VkRenderingAttachmentInfo` whose
/// `imageView` is `VK_NULL_HANDLE` is a color slot that is present (it
/// still counts against the pipeline's `colorAttachmentCount`) but unused
/// -- the write to it must be silently discarded rather than the draw
/// being rejected for lacking a bound image there.
TEST_F(DrawTest, DynamicRenderingSkipsNullColorAttachment) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(DualOutputFragmentSource);

  VkFormat ColorFormats[2] = {VK_FORMAT_R8G8B8A8_UNORM,
                              VK_FORMAT_R8G8B8A8_UNORM};
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 2;
  Rendering.pColorAttachmentFormats = ColorFormats;

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachments[2]{};
  BlendAttachments[0].colorWriteMask = 0xF;
  BlendAttachments[1].colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 2;
  Blend.pAttachments = BlendAttachments;
  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.layout = Layout;
  Info.pNext = &Rendering;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);

  VkRenderingAttachmentInfo ColorAttachments[2]{};
  ColorAttachments[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  ColorAttachments[0].imageView = ColorView;
  ColorAttachments[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  ColorAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  ColorAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  ColorAttachments[0].clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  // Present (counts against `colorAttachmentCount`) but unused.
  ColorAttachments[1].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  ColorAttachments[1].imageView = VK_NULL_HANDLE;
  ColorAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  ColorAttachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;

  VkRenderingInfo RenderingInfo{};
  RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  RenderingInfo.renderArea = {{0, 0}, {Extent, Extent}};
  RenderingInfo.layerCount = 1;
  RenderingInfo.colorAttachmentCount = 2;
  RenderingInfo.pColorAttachments = ColorAttachments;

  vkCmdBeginRenderingKHR(Cmd, &RenderingInfo);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderingKHR(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      // SV_Target0's solid red lands in the one real attachment;
      // SV_Target1's solid green is simply discarded (no crash, no image
      // needed for the unused slot).
      EXPECT_EQ(texel(X, Y)[0], 0xFF) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(texel(X, Y)[1], 0x00) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap H7s) A classic `VkRenderPass` subpass's color attachment list
/// may name `VK_ATTACHMENT_UNUSED` for a slot: present (still counts
/// against `colorAttachmentCount`/the fragment stage's own output
/// locations) but backed by no real attachment at all, the same "present
/// but unused" concept `DynamicRenderingSkipsNullColorAttachment` above
/// already exercises for dynamic rendering (roadmap E5)'s
/// `VK_NULL_HANDLE` imageView. Discovered via a real
/// `dEQP-VK.pipeline.monolithic.multisample.alpha_to_coverage_unused_attachment.*`
/// re-run (roadmap H7r), whose subpass writes to fragment output location
/// 1 while leaving location 0's slot unused.
TEST_F(DrawTest, ClassicRenderPassSkipsUnusedColorAttachment) {
  VkAttachmentDescription Attachment{};
  Attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  Attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  // Location 0 is unused; location 1 is the render pass's one real
  // attachment (index 0 into `pAttachments`).
  VkAttachmentReference ColorRefs[2] = {
      {VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED},
      {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 2;
  Subpass.pColorAttachments = ColorRefs;
  VkRenderPassCreateInfo PassInfo{};
  PassInfo.attachmentCount = 1;
  PassInfo.pAttachments = &Attachment;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass LocalPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &LocalPass),
            VK_SUCCESS);

  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = LocalPass;
  FbInfo.attachmentCount = 1;
  FbInfo.pAttachments = &ColorView;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer LocalFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &LocalFb),
            VK_SUCCESS);

  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(DualOutputFragmentSource);
  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  // One blend state per subpass color-attachment slot, including the
  // unused one -- `VkPipelineColorBlendStateCreateInfo::attachmentCount`
  // must match the subpass's own `colorAttachmentCount` regardless of
  // which slots are actually backed by a real attachment.
  VkPipelineColorBlendAttachmentState BlendAttachments[2]{};
  BlendAttachments[0].colorWriteMask = 0xF;
  BlendAttachments[1].colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 2;
  Blend.pAttachments = BlendAttachments;
  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.layout = Layout;
  Info.renderPass = LocalPass;
  Info.subpass = 0;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValue{};
  ClearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = LocalPass;
  PassBegin.framebuffer = LocalFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 1;
  PassBegin.pClearValues = &ClearValue;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      // SV_Target1's solid green lands in the one real attachment;
      // SV_Target0's solid red is simply discarded (no crash, no image
      // needed for the unused slot).
      EXPECT_EQ(texel(X, Y)[0], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(texel(X, Y)[1], 0xFF) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyFramebuffer(Device, LocalFb, nullptr);
  vkDestroyRenderPass(Device, LocalPass, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// The driver advertises `VK_KHR_dynamic_rendering` and
/// `VK_EXT_extended_dynamic_state` (roadmap C4c) and accepts either at
/// device creation; anything it does not implement is still refused.
TEST_F(DrawTest, AdvertisesDynamicRenderingExtension) {
  uint32_t Count = 0;
  ASSERT_EQ(
      vkEnumerateDeviceExtensionProperties(Physical, nullptr, &Count, nullptr),
      VK_SUCCESS);
  ASSERT_EQ(Count, 32u);
  std::vector<VkExtensionProperties> Properties(Count);
  ASSERT_EQ(vkEnumerateDeviceExtensionProperties(Physical, nullptr, &Count,
                                                 Properties.data()),
            VK_SUCCESS);
  auto HasExtension = [&](const char *Name) {
    for (const VkExtensionProperties &P : Properties)
      if (std::strcmp(P.extensionName, Name) == 0)
        return true;
    return false;
  };
  EXPECT_TRUE(HasExtension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME));
  EXPECT_TRUE(HasExtension(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME));
  // Roadmap E3.
  EXPECT_TRUE(HasExtension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME));
  // Roadmap E5.
  EXPECT_TRUE(HasExtension(VK_KHR_MAINTENANCE_5_EXTENSION_NAME));
  // Roadmap E6.
  EXPECT_TRUE(HasExtension(VK_KHR_MAINTENANCE_6_EXTENSION_NAME));
  // Roadmap E8.
  EXPECT_TRUE(HasExtension(VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME));
  // Roadmap E9.
  EXPECT_TRUE(
      HasExtension(VK_EXT_PIPELINE_CREATION_CACHE_CONTROL_EXTENSION_NAME));
  // Roadmap E10.
  EXPECT_TRUE(HasExtension(VK_EXT_PRIVATE_DATA_EXTENSION_NAME));
  // Roadmap E11.
  EXPECT_TRUE(
      HasExtension(VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME));
  // Roadmap E12.
  EXPECT_TRUE(HasExtension(VK_KHR_SHADER_TERMINATE_INVOCATION_EXTENSION_NAME));
  // Roadmap E13.
  EXPECT_TRUE(
      HasExtension(VK_KHR_ZERO_INITIALIZE_WORKGROUP_MEMORY_EXTENSION_NAME));
  // Roadmap E14.
  EXPECT_TRUE(HasExtension(VK_EXT_INLINE_UNIFORM_BLOCK_EXTENSION_NAME));
  // Roadmap E18.
  EXPECT_TRUE(HasExtension(VK_EXT_TEXEL_BUFFER_ALIGNMENT_EXTENSION_NAME));
  // Roadmap E19.
  EXPECT_TRUE(HasExtension(VK_EXT_4444_FORMATS_EXTENSION_NAME));
  EXPECT_TRUE(HasExtension(VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME));
  EXPECT_TRUE(HasExtension(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME));
  EXPECT_TRUE(HasExtension(VK_EXT_TOOLING_INFO_EXTENSION_NAME));
  // Roadmap F1.
  EXPECT_TRUE(HasExtension(VK_KHR_GLOBAL_PRIORITY_EXTENSION_NAME));
  // Roadmap F2.
  EXPECT_TRUE(HasExtension(VK_KHR_SHADER_SUBGROUP_ROTATE_EXTENSION_NAME));
  // Roadmap F4.
  EXPECT_TRUE(HasExtension(VK_KHR_SHADER_EXPECT_ASSUME_EXTENSION_NAME));
  // Roadmap F6.
  EXPECT_TRUE(HasExtension(VK_KHR_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME));
  // Roadmap F7.
  EXPECT_TRUE(HasExtension(VK_KHR_INDEX_TYPE_UINT8_EXTENSION_NAME));
  // Roadmap F8/F8a.
  EXPECT_TRUE(HasExtension(VK_KHR_DYNAMIC_RENDERING_LOCAL_READ_EXTENSION_NAME));
  // Roadmap F9.
  EXPECT_TRUE(HasExtension(VK_EXT_PIPELINE_PROTECTED_ACCESS_EXTENSION_NAME));
  // Roadmap F10.
  EXPECT_TRUE(HasExtension(VK_EXT_PIPELINE_ROBUSTNESS_EXTENSION_NAME));
  // Roadmap F11.
  EXPECT_TRUE(HasExtension(VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME));
  // Roadmap F12.
  EXPECT_TRUE(HasExtension(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME));
  // Roadmap F13.
  EXPECT_TRUE(HasExtension(VK_KHR_LOAD_STORE_OP_NONE_EXTENSION_NAME));
  // Roadmap F14.
  EXPECT_TRUE(HasExtension(VK_KHR_MAP_MEMORY_2_EXTENSION_NAME));
  // Roadmap H2.
  EXPECT_TRUE(HasExtension(VK_KHR_MULTIVIEW_EXTENSION_NAME));

  VkPhysicalDeviceDynamicRenderingFeatures Features{};
  Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
  VkPhysicalDeviceExtendedDynamicStateFeaturesEXT ExtDynState{};
  ExtDynState.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
  Features.pNext = &ExtDynState;
  VkPhysicalDeviceFeatures2 Features2{};
  Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  Features2.pNext = &Features;
  vkGetPhysicalDeviceFeatures2(Physical, &Features2);
  EXPECT_EQ(Features.dynamicRendering, VK_TRUE);
  EXPECT_EQ(ExtDynState.extendedDynamicState, VK_TRUE);

  const char *Enabled = VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
  VkDeviceCreateInfo DevInfo{};
  DevInfo.enabledExtensionCount = 1;
  DevInfo.ppEnabledExtensionNames = &Enabled;
  VkDevice Second = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Second), VK_SUCCESS);
  vkDestroyDevice(Second, nullptr);

  Enabled = VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME;
  EXPECT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Second), VK_SUCCESS);
  vkDestroyDevice(Second, nullptr);

  // Roadmap E3.
  Enabled = VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME;
  EXPECT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Second), VK_SUCCESS);
  vkDestroyDevice(Second, nullptr);

  const char *Unsupported = "VK_KHR_swapchain";
  DevInfo.ppEnabledExtensionNames = &Unsupported;
  EXPECT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Second),
            VK_ERROR_EXTENSION_NOT_PRESENT);
}

/// Roadmap F1: `VkDeviceQueueGlobalPriorityCreateInfo`'s `globalPriority`
/// hint is a no-op at device creation -- this ICD has one worker pool with
/// no real OS-level scheduling priority, so every mandatory priority level
/// (all four the query above reports as supported) is honored the same
/// way, matching the "single logical queue, narrowed by capability flags
/// only" precedent roadmap C7 set.
TEST_F(DrawTest, GlobalPriorityCreateInfoIsANoOpAtDeviceCreation) {
  const char *Enabled = VK_KHR_GLOBAL_PRIORITY_EXTENSION_NAME;
  const float QueuePriority = 1.0f;
  const VkQueueGlobalPriority Priorities[] = {
      VK_QUEUE_GLOBAL_PRIORITY_LOW, VK_QUEUE_GLOBAL_PRIORITY_MEDIUM,
      VK_QUEUE_GLOBAL_PRIORITY_HIGH, VK_QUEUE_GLOBAL_PRIORITY_REALTIME};
  for (VkQueueGlobalPriority Priority : Priorities) {
    VkDeviceQueueGlobalPriorityCreateInfo GlobalPriorityInfo{};
    GlobalPriorityInfo.sType =
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO;
    GlobalPriorityInfo.globalPriority = Priority;

    VkDeviceQueueCreateInfo QueueInfo{};
    QueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    QueueInfo.pNext = &GlobalPriorityInfo;
    QueueInfo.queueFamilyIndex = 0;
    QueueInfo.queueCount = 1;
    QueueInfo.pQueuePriorities = &QueuePriority;

    VkDeviceCreateInfo DevInfo{};
    DevInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    DevInfo.queueCreateInfoCount = 1;
    DevInfo.pQueueCreateInfos = &QueueInfo;
    DevInfo.enabledExtensionCount = 1;
    DevInfo.ppEnabledExtensionNames = &Enabled;

    VkDevice Second = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateDevice(Physical, &DevInfo, nullptr, &Second), VK_SUCCESS);
    VkQueue Queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(Second, 0, 0, &Queue);
    EXPECT_NE(Queue, VK_NULL_HANDLE);
    vkDestroyDevice(Second, nullptr);
  }
}

/// `vkCmdClearAttachments` clears the bound attachment over its rectangles,
/// inside the render pass instance, after a draw has already written it.
TEST_F(DrawTest, ClearsAttachmentInsideRenderPass) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  VkClearAttachment Clear{};
  Clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  Clear.colorAttachment = 0;
  Clear.clearValue.color = {{0.0f, 0.0f, 1.0f, 1.0f}};
  VkClearRect Rect{};
  Rect.rect = {{0, 0}, {2, 2}};
  Rect.layerCount = 1;
  vkCmdClearAttachments(Cmd, 1, &Clear, 1, &Rect);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // Inside the cleared rectangle: blue. Outside it: the draw's red.
  EXPECT_EQ(texel(0, 0)[2], 0xFF);
  EXPECT_EQ(texel(0, 0)[0], 0x00);
  EXPECT_EQ(texel(3, 3)[0], 0xFF);
  EXPECT_EQ(texel(3, 3)[2], 0x00);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// `DepthState::TestEnable`/`WriteEnable`: a nearer draw's depth write
/// (`CompareOp::Less`) rejects a farther draw covering the same pixels, so
/// the nearer draw's color survives -- the completion scenario's own "depth"
/// bullet (see "V6: Graphics queue and basic rendering" in
/// feme/docs/FeMeVulkanDesign.md).
TEST_F(DrawTest, RendersWithDepthTest) {
  VkImage DepthImage = VK_NULL_HANDLE;
  VkImageView DepthView = VK_NULL_HANDLE;
  VkDeviceMemory DepthMemory = VK_NULL_HANDLE;
  createImageAndView(
      VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_DEPTH_BIT, DepthImage, DepthView, DepthMemory);

  VkAttachmentDescription Attachments[2]{};
  Attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  Attachments[1].format = VK_FORMAT_D32_SFLOAT;
  Attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference DepthRef{
      1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 1;
  Subpass.pColorAttachments = &ColorRef;
  Subpass.pDepthStencilAttachment = &DepthRef;
  VkRenderPassCreateInfo PassInfo{};
  PassInfo.attachmentCount = 2;
  PassInfo.pAttachments = Attachments;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass LocalPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &LocalPass),
            VK_SUCCESS);

  VkImageView FbViews[2] = {ColorView, DepthView};
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = LocalPass;
  FbInfo.attachmentCount = 2;
  FbInfo.pAttachments = FbViews;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer LocalFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &LocalFb),
            VK_SUCCESS);

  VkPipelineDepthStencilStateCreateInfo DepthStencil{};
  DepthStencil.depthTestEnable = VK_TRUE;
  DepthStencil.depthWriteEnable = VK_TRUE;
  DepthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

  auto makePipeline = [&](llvm::StringRef VertexSource,
                          llvm::StringRef FragmentSource) {
    VkShaderModule Vertex = createModule(VertexSource);
    VkShaderModule Fragment = createModule(FragmentSource);
    VkPipelineShaderStageCreateInfo Stages[2]{};
    Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    Stages[0].module = Vertex;
    Stages[0].pName = "main";
    Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    Stages[1].module = Fragment;
    Stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo VertexInput{};
    VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
    InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
    VkRect2D Scissor{{0, 0}, {Extent, Extent}};
    VkPipelineViewportStateCreateInfo ViewportState{};
    ViewportState.viewportCount = 1;
    ViewportState.pViewports = &Viewport;
    ViewportState.scissorCount = 1;
    ViewportState.pScissors = &Scissor;
    VkPipelineRasterizationStateCreateInfo Raster{};
    Raster.cullMode = VK_CULL_MODE_NONE;
    Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Raster.polygonMode = VK_POLYGON_MODE_FILL;
    VkPipelineMultisampleStateCreateInfo Multisample{};
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState BlendAttachment{};
    BlendAttachment.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo Blend{};
    Blend.attachmentCount = 1;
    Blend.pAttachments = &BlendAttachment;
    VkGraphicsPipelineCreateInfo Info{};
    Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    Info.stageCount = 2;
    Info.pStages = Stages;
    Info.pVertexInputState = &VertexInput;
    Info.pInputAssemblyState = &InputAssembly;
    Info.pViewportState = &ViewportState;
    Info.pRasterizationState = &Raster;
    Info.pMultisampleState = &Multisample;
    Info.pDepthStencilState = &DepthStencil;
    Info.pColorBlendState = &Blend;
    Info.layout = Layout;
    Info.renderPass = LocalPass;
    VkPipeline Pipe = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info,
                                        nullptr, &Pipe),
              VK_SUCCESS);
    vkDestroyShaderModule(Device, Fragment, nullptr);
    vkDestroyShaderModule(Device, Vertex, nullptr);
    return Pipe;
  };
  VkPipeline NearRed = makePipeline(NearDepthVertexSource, RedFragmentSource);
  VkPipeline FarGreen = makePipeline(FarDepthVertexSource, GreenFragmentSource);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValues[2]{};
  ClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  ClearValues[1].depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = LocalPass;
  PassBegin.framebuffer = LocalFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 2;
  PassBegin.pClearValues = ClearValues;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  // The nearer (red) draw first, writing depth 0.2; the farther (green)
  // draw second, rejected by the depth test since 0.8 is not less than 0.2.
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, NearRed);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, FarGreen);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel = texel(X, Y);
      EXPECT_EQ(Texel[0], 0xFF) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[1], 0x00) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, FarGreen, nullptr);
  vkDestroyPipeline(Device, NearRed, nullptr);
  vkDestroyFramebuffer(Device, LocalFb, nullptr);
  vkDestroyRenderPass(Device, LocalPass, nullptr);
  vkDestroyImageView(Device, DepthView, nullptr);
  vkDestroyImage(Device, DepthImage, nullptr);
  vkFreeMemory(Device, DepthMemory, nullptr);
}

/// (roadmap H7d) `RasterState::DepthClampEnable`: a triangle placed beyond
/// the far plane (`FarBeyondDepthVertexSource`'s NDC Z of 2.0) is clipped
/// away entirely -- no fragment written at all -- when depth clamp is
/// disabled, but survives (its own depth clamped to the viewport's
/// `maxDepth` instead) when `depthClampEnable` is set.
TEST_F(DrawTest, DepthClampKeepsFragmentsBeyondTheFarPlane) {
  VkImage DepthImage = VK_NULL_HANDLE;
  VkImageView DepthView = VK_NULL_HANDLE;
  VkDeviceMemory DepthMemory = VK_NULL_HANDLE;
  createImageAndView(
      VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_DEPTH_BIT, DepthImage, DepthView, DepthMemory);

  VkAttachmentDescription Attachments[2]{};
  Attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  Attachments[1].format = VK_FORMAT_D32_SFLOAT;
  Attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference DepthRef{
      1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 1;
  Subpass.pColorAttachments = &ColorRef;
  Subpass.pDepthStencilAttachment = &DepthRef;
  VkRenderPassCreateInfo PassInfo{};
  PassInfo.attachmentCount = 2;
  PassInfo.pAttachments = Attachments;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass LocalPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &LocalPass),
            VK_SUCCESS);

  VkImageView FbViews[2] = {ColorView, DepthView};
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = LocalPass;
  FbInfo.attachmentCount = 2;
  FbInfo.pAttachments = FbViews;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer LocalFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &LocalFb),
            VK_SUCCESS);

  auto makePipeline = [&](bool DepthClampEnable) {
    VkShaderModule Vertex = createModule(FarBeyondDepthVertexSource);
    VkShaderModule Fragment = createModule(RedFragmentSource);
    VkPipelineShaderStageCreateInfo Stages[2]{};
    Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    Stages[0].module = Vertex;
    Stages[0].pName = "main";
    Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    Stages[1].module = Fragment;
    Stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo VertexInput{};
    VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
    InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
    VkRect2D Scissor{{0, 0}, {Extent, Extent}};
    VkPipelineViewportStateCreateInfo ViewportState{};
    ViewportState.viewportCount = 1;
    ViewportState.pViewports = &Viewport;
    ViewportState.scissorCount = 1;
    ViewportState.pScissors = &Scissor;
    VkPipelineRasterizationStateCreateInfo Raster{};
    Raster.cullMode = VK_CULL_MODE_NONE;
    Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Raster.polygonMode = VK_POLYGON_MODE_FILL;
    Raster.depthClampEnable = DepthClampEnable;
    VkPipelineMultisampleStateCreateInfo Multisample{};
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo DepthStencil{};
    DepthStencil.depthTestEnable = VK_TRUE;
    DepthStencil.depthWriteEnable = VK_TRUE;
    DepthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState BlendAttachment{};
    BlendAttachment.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo Blend{};
    Blend.attachmentCount = 1;
    Blend.pAttachments = &BlendAttachment;
    VkGraphicsPipelineCreateInfo Info{};
    Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    Info.stageCount = 2;
    Info.pStages = Stages;
    Info.pVertexInputState = &VertexInput;
    Info.pInputAssemblyState = &InputAssembly;
    Info.pViewportState = &ViewportState;
    Info.pRasterizationState = &Raster;
    Info.pMultisampleState = &Multisample;
    Info.pDepthStencilState = &DepthStencil;
    Info.pColorBlendState = &Blend;
    Info.layout = Layout;
    Info.renderPass = LocalPass;
    VkPipeline Pipe = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info,
                                        nullptr, &Pipe),
              VK_SUCCESS);
    vkDestroyShaderModule(Device, Fragment, nullptr);
    vkDestroyShaderModule(Device, Vertex, nullptr);
    return Pipe;
  };

  auto renderAndSampleTopLeft = [&](VkPipeline Pipe) {
    VkCommandBufferBeginInfo BeginInfo{};
    EXPECT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
    VkClearValue ClearValues[2]{};
    ClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    ClearValues[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo PassBegin{};
    PassBegin.renderPass = LocalPass;
    PassBegin.framebuffer = LocalFb;
    PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
    PassBegin.clearValueCount = 2;
    PassBegin.pClearValues = ClearValues;
    vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
    vkCmdDraw(Cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(Cmd);
    EXPECT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
    EXPECT_EQ(submit(), VK_SUCCESS);
    return texel(0, 0);
  };

  // Without depth clamp: the triangle is clipped away entirely (its NDC Z
  // of 2.0 is beyond the far plane), so the clear color (black) survives.
  // (The clear color's own alpha is 1.0, so only the R channel
  // distinguishes the clear color from the red draw here.)
  VkPipeline Clipped = makePipeline(/*DepthClampEnable=*/false);
  std::array<uint8_t, 4> ClippedTexel = renderAndSampleTopLeft(Clipped);
  EXPECT_EQ(ClippedTexel[0], 0x00);

  // With depth clamp: the same triangle survives, its depth clamped to the
  // viewport's own maxDepth (1.0) instead of being clipped.
  VkPipeline Clamped = makePipeline(/*DepthClampEnable=*/true);
  std::array<uint8_t, 4> ClampedTexel = renderAndSampleTopLeft(Clamped);
  EXPECT_EQ(ClampedTexel[0], 0xFF);

  vkDestroyPipeline(Device, Clamped, nullptr);
  vkDestroyPipeline(Device, Clipped, nullptr);
  vkDestroyFramebuffer(Device, LocalFb, nullptr);
  vkDestroyRenderPass(Device, LocalPass, nullptr);
  vkDestroyImageView(Device, DepthView, nullptr);
  vkDestroyImage(Device, DepthImage, nullptr);
  vkFreeMemory(Device, DepthMemory, nullptr);
}

/// (roadmap H7d) Regression test for a real `deqp-vk` reproduction of
/// `dEQP-VK.clipping.clip_volume.depth_clamp.*`: depth clamp must be
/// applied to the *interpolated* per-fragment depth, not to each vertex's
/// own depth before interpolation. `MixedDepthVertexSource`'s single
/// triangle spans clip-space Z from -3.0 (vertex 0, below the near plane)
/// through 0.5 (vertex 2, in range) to 5.0 (vertex 1, beyond the far
/// plane); `FragCoordZFragmentSource` writes the interpolated,
/// post-clamp `gl_FragCoord.z` into the color attachment's own R channel.
/// A pre-fix, per-vertex-clamp-then-interpolate implementation would
/// produce a false linear ramp between the two out-of-range vertices'
/// own clamped endpoints (0.0 and 1.0) with no flat region at either
/// end; the correct, per-fragment-clamped result has a genuine flat
/// region wherever the *raw* interpolated depth is still out of range.
TEST_F(DrawTest, DepthClampAppliesAfterInterpolationNotBeforeIt) {
  VkImage DepthImage = VK_NULL_HANDLE;
  VkImageView DepthView = VK_NULL_HANDLE;
  VkDeviceMemory DepthMemory = VK_NULL_HANDLE;
  createImageAndView(
      VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_DEPTH_BIT, DepthImage, DepthView, DepthMemory);

  VkAttachmentDescription Attachments[2]{};
  Attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  Attachments[1].format = VK_FORMAT_D32_SFLOAT;
  Attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference DepthRef{
      1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 1;
  Subpass.pColorAttachments = &ColorRef;
  Subpass.pDepthStencilAttachment = &DepthRef;
  VkRenderPassCreateInfo PassInfo{};
  PassInfo.attachmentCount = 2;
  PassInfo.pAttachments = Attachments;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass LocalPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &LocalPass),
            VK_SUCCESS);

  VkImageView FbViews[2] = {ColorView, DepthView};
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = LocalPass;
  FbInfo.attachmentCount = 2;
  FbInfo.pAttachments = FbViews;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer LocalFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &LocalFb),
            VK_SUCCESS);

  VkShaderModule Vertex = createModule(MixedDepthVertexSource);
  VkShaderModule Fragment = createModule(FragCoordZFragmentSource);
  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  Raster.depthClampEnable = VK_TRUE;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo DepthStencil{};
  DepthStencil.depthTestEnable = VK_TRUE;
  DepthStencil.depthWriteEnable = VK_TRUE;
  DepthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;
  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pDepthStencilState = &DepthStencil;
  Info.pColorBlendState = &Blend;
  Info.layout = Layout;
  Info.renderPass = LocalPass;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info,
                                      nullptr, &Pipe),
            VK_SUCCESS);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValues[2]{};
  ClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  ClearValues[1].depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = LocalPass;
  PassBegin.framebuffer = LocalFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 2;
  PassBegin.pClearValues = ClearValues;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // Vertex 0's own screen-space corner (top-left, NDC (-1,-1) under
  // `OriginUpperLeft`) has clip-space Z = -3.0 -- deeply below the near
  // plane. A pre-fix, per-vertex-clamp-then-interpolate implementation
  // linearly ramps from 0.0 (vertex 0's own clamped value) towards 1.0
  // (vertex 1's own clamped value) starting immediately at vertex 0's own
  // corner, with no flat region; the fix's correct, per-fragment clamp
  // instead stays flatly 0 across the whole region where the *raw*
  // interpolated depth remains below 0 (which, given the geometry here,
  // covers a full quarter of the attachment's own width nearest vertex 0).
  // Column 0 must read exactly 0 under both implementations (the ramp
  // itself starts at 0 there too), but column 1 -- one quarter of the way
  // across -- distinguishes them: the buggy ramp has already risen to a
  // clearly nonzero byte value there, while the fix stays exactly 0.
  EXPECT_EQ(texel(0, 0)[0], 0x00);
  EXPECT_EQ(texel(1, 0)[0], 0x00);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyFramebuffer(Device, LocalFb, nullptr);
  vkDestroyRenderPass(Device, LocalPass, nullptr);
  vkDestroyImageView(Device, DepthView, nullptr);
  vkDestroyImage(Device, DepthImage, nullptr);
  vkFreeMemory(Device, DepthMemory, nullptr);
}

/// (roadmap H7d) `RasterState::DepthBiasEnable`: two triangles at the exact
/// same depth (`NearDepthVertexSource`'s fixed NDC Z of 0.2) would tie
/// under `CompareOp::Less` (the second draw's own equal depth fails the
/// test) -- but the second draw's own large negative
/// `depthBiasConstantFactor` pushes its biased depth below the first
/// draw's, so it passes and overwrites the first draw's color.
TEST_F(DrawTest, DepthBiasShiftsOverlappingDepth) {
  VkImage DepthImage = VK_NULL_HANDLE;
  VkImageView DepthView = VK_NULL_HANDLE;
  VkDeviceMemory DepthMemory = VK_NULL_HANDLE;
  createImageAndView(
      VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_DEPTH_BIT, DepthImage, DepthView, DepthMemory);

  VkAttachmentDescription Attachments[2]{};
  Attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  Attachments[1].format = VK_FORMAT_D32_SFLOAT;
  Attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference DepthRef{
      1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 1;
  Subpass.pColorAttachments = &ColorRef;
  Subpass.pDepthStencilAttachment = &DepthRef;
  VkRenderPassCreateInfo PassInfo{};
  PassInfo.attachmentCount = 2;
  PassInfo.pAttachments = Attachments;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass LocalPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &LocalPass),
            VK_SUCCESS);

  VkImageView FbViews[2] = {ColorView, DepthView};
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = LocalPass;
  FbInfo.attachmentCount = 2;
  FbInfo.pAttachments = FbViews;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer LocalFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &LocalFb),
            VK_SUCCESS);

  auto makePipeline = [&](llvm::StringRef FragmentSource, bool BiasEnable) {
    VkShaderModule Vertex = createModule(NearDepthVertexSource);
    VkShaderModule Fragment = createModule(FragmentSource);
    VkPipelineShaderStageCreateInfo Stages[2]{};
    Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    Stages[0].module = Vertex;
    Stages[0].pName = "main";
    Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    Stages[1].module = Fragment;
    Stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo VertexInput{};
    VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
    InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
    VkRect2D Scissor{{0, 0}, {Extent, Extent}};
    VkPipelineViewportStateCreateInfo ViewportState{};
    ViewportState.viewportCount = 1;
    ViewportState.pViewports = &Viewport;
    ViewportState.scissorCount = 1;
    ViewportState.pScissors = &Scissor;
    VkPipelineRasterizationStateCreateInfo Raster{};
    Raster.cullMode = VK_CULL_MODE_NONE;
    Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Raster.polygonMode = VK_POLYGON_MODE_FILL;
    Raster.depthBiasEnable = BiasEnable;
    // A huge negative constant factor guarantees a visible shift
    // regardless of `D32_FLOAT`'s own tiny per-value ULP.
    Raster.depthBiasConstantFactor = -1.0e7f;
    VkPipelineMultisampleStateCreateInfo Multisample{};
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo DepthStencil{};
    DepthStencil.depthTestEnable = VK_TRUE;
    DepthStencil.depthWriteEnable = VK_TRUE;
    DepthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState BlendAttachment{};
    BlendAttachment.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo Blend{};
    Blend.attachmentCount = 1;
    Blend.pAttachments = &BlendAttachment;
    VkGraphicsPipelineCreateInfo Info{};
    Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    Info.stageCount = 2;
    Info.pStages = Stages;
    Info.pVertexInputState = &VertexInput;
    Info.pInputAssemblyState = &InputAssembly;
    Info.pViewportState = &ViewportState;
    Info.pRasterizationState = &Raster;
    Info.pMultisampleState = &Multisample;
    Info.pDepthStencilState = &DepthStencil;
    Info.pColorBlendState = &Blend;
    Info.layout = Layout;
    Info.renderPass = LocalPass;
    VkPipeline Pipe = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info,
                                        nullptr, &Pipe),
              VK_SUCCESS);
    vkDestroyShaderModule(Device, Fragment, nullptr);
    vkDestroyShaderModule(Device, Vertex, nullptr);
    return Pipe;
  };
  VkPipeline Base = makePipeline(RedFragmentSource, /*BiasEnable=*/false);
  VkPipeline Biased = makePipeline(GreenFragmentSource, /*BiasEnable=*/true);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValues[2]{};
  ClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  ClearValues[1].depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = LocalPass;
  PassBegin.framebuffer = LocalFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 2;
  PassBegin.pClearValues = ClearValues;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  // The base (red) draw first, writing depth 0.2; the biased (green) draw
  // second, at the exact same nominal depth -- only its own negative bias
  // lets it pass `CompareOp::Less` and overwrite the base draw's color.
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Base);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Biased);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel = texel(X, Y);
      EXPECT_EQ(Texel[0], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[1], 0xFF) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Biased, nullptr);
  vkDestroyPipeline(Device, Base, nullptr);
  vkDestroyFramebuffer(Device, LocalFb, nullptr);
  vkDestroyRenderPass(Device, LocalPass, nullptr);
  vkDestroyImageView(Device, DepthView, nullptr);
  vkDestroyImage(Device, DepthImage, nullptr);
  vkFreeMemory(Device, DepthMemory, nullptr);
}

/// (roadmap H7d) `DepthState::BoundsTestEnable`: the depth bounds test
/// compares the value *already stored* in the depth attachment (here, the
/// render pass's own clear value of 0.9) against `[MinDepthBounds,
/// MaxDepthBounds]`, not the incoming fragment's own depth -- 0.9 sits
/// outside `[0.0, 0.5]`, so the draw's fragment is discarded entirely and
/// the clear color survives.
TEST_F(DrawTest, DepthBoundsTestRejectsOutOfRangeFragments) {
  VkImage DepthImage = VK_NULL_HANDLE;
  VkImageView DepthView = VK_NULL_HANDLE;
  VkDeviceMemory DepthMemory = VK_NULL_HANDLE;
  createImageAndView(
      VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_DEPTH_BIT, DepthImage, DepthView, DepthMemory);

  VkAttachmentDescription Attachments[2]{};
  Attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  Attachments[1].format = VK_FORMAT_D32_SFLOAT;
  Attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference DepthRef{
      1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 1;
  Subpass.pColorAttachments = &ColorRef;
  Subpass.pDepthStencilAttachment = &DepthRef;
  VkRenderPassCreateInfo PassInfo{};
  PassInfo.attachmentCount = 2;
  PassInfo.pAttachments = Attachments;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass LocalPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &LocalPass),
            VK_SUCCESS);

  VkImageView FbViews[2] = {ColorView, DepthView};
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = LocalPass;
  FbInfo.attachmentCount = 2;
  FbInfo.pAttachments = FbViews;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer LocalFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &LocalFb),
            VK_SUCCESS);

  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo DepthStencil{};
  DepthStencil.depthBoundsTestEnable = VK_TRUE;
  DepthStencil.minDepthBounds = 0.0f;
  DepthStencil.maxDepthBounds = 0.5f;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;
  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pDepthStencilState = &DepthStencil;
  Info.pColorBlendState = &Blend;
  Info.layout = Layout;
  Info.renderPass = LocalPass;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info,
                                      nullptr, &Pipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValues[2]{};
  ClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  ClearValues[1].depthStencil = {0.9f, 0};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = LocalPass;
  PassBegin.framebuffer = LocalFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 2;
  PassBegin.pClearValues = ClearValues;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // The clear color's own alpha is 1.0, so only the R channel
  // distinguishes the clear color from the (rejected) red draw here.
  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel = texel(X, Y);
      EXPECT_EQ(Texel[0], 0x00) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyFramebuffer(Device, LocalFb, nullptr);
  vkDestroyRenderPass(Device, LocalPass, nullptr);
  vkDestroyImageView(Device, DepthView, nullptr);
  vkDestroyImage(Device, DepthImage, nullptr);
  vkFreeMemory(Device, DepthMemory, nullptr);
}

/// `StencilState::TestEnable`: a first draw (`CompareOp::Always`,
/// `StencilOp::Replace`) writes a stencil reference over half the render
/// area; a second draw (`CompareOp::Equal`) then only reaches the half whose
/// stencil value matches -- the completion scenario's own "stencil" bullet.
TEST_F(DrawTest, RendersWithStencilTest) {
  VkImage StencilImage = VK_NULL_HANDLE;
  VkImageView StencilView = VK_NULL_HANDLE;
  VkDeviceMemory StencilMemory = VK_NULL_HANDLE;
  createImageAndView(
      VK_FORMAT_S8_UINT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_STENCIL_BIT, StencilImage, StencilView, StencilMemory);

  VkAttachmentDescription Attachments[2]{};
  Attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  Attachments[1].format = VK_FORMAT_S8_UINT;
  Attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference StencilRef{
      1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 1;
  Subpass.pColorAttachments = &ColorRef;
  Subpass.pDepthStencilAttachment = &StencilRef;
  VkRenderPassCreateInfo PassInfo{};
  PassInfo.attachmentCount = 2;
  PassInfo.pAttachments = Attachments;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass LocalPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &LocalPass),
            VK_SUCCESS);

  VkImageView FbViews[2] = {ColorView, StencilView};
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = LocalPass;
  FbInfo.attachmentCount = 2;
  FbInfo.pAttachments = FbViews;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer LocalFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &LocalFb),
            VK_SUCCESS);

  auto makePipeline = [&](llvm::StringRef FragmentSource,
                          const VkRect2D &Scissor, VkCompareOp Compare,
                          VkStencilOp PassOp) {
    VkShaderModule Vertex = createModule(FullscreenVertexSource);
    VkShaderModule Fragment = createModule(FragmentSource);
    VkPipelineShaderStageCreateInfo Stages[2]{};
    Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    Stages[0].module = Vertex;
    Stages[0].pName = "main";
    Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    Stages[1].module = Fragment;
    Stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo VertexInput{};
    VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
    InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
    VkRect2D LocalScissor = Scissor;
    VkPipelineViewportStateCreateInfo ViewportState{};
    ViewportState.viewportCount = 1;
    ViewportState.pViewports = &Viewport;
    ViewportState.scissorCount = 1;
    ViewportState.pScissors = &LocalScissor;
    VkPipelineRasterizationStateCreateInfo Raster{};
    Raster.cullMode = VK_CULL_MODE_NONE;
    Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Raster.polygonMode = VK_POLYGON_MODE_FILL;
    VkPipelineMultisampleStateCreateInfo Multisample{};
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState BlendAttachment{};
    BlendAttachment.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo Blend{};
    Blend.attachmentCount = 1;
    Blend.pAttachments = &BlendAttachment;
    VkStencilOpState Face{};
    Face.failOp = VK_STENCIL_OP_KEEP;
    Face.passOp = PassOp;
    Face.depthFailOp = VK_STENCIL_OP_KEEP;
    Face.compareOp = Compare;
    Face.compareMask = 0xFF;
    Face.writeMask = 0xFF;
    Face.reference = 1;
    VkPipelineDepthStencilStateCreateInfo DepthStencil{};
    DepthStencil.stencilTestEnable = VK_TRUE;
    DepthStencil.front = Face;
    DepthStencil.back = Face;
    VkGraphicsPipelineCreateInfo Info{};
    Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    Info.stageCount = 2;
    Info.pStages = Stages;
    Info.pVertexInputState = &VertexInput;
    Info.pInputAssemblyState = &InputAssembly;
    Info.pViewportState = &ViewportState;
    Info.pRasterizationState = &Raster;
    Info.pMultisampleState = &Multisample;
    Info.pDepthStencilState = &DepthStencil;
    Info.pColorBlendState = &Blend;
    Info.layout = Layout;
    Info.renderPass = LocalPass;
    VkPipeline Pipe = VK_NULL_HANDLE;
    EXPECT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info,
                                        nullptr, &Pipe),
              VK_SUCCESS);
    vkDestroyShaderModule(Device, Fragment, nullptr);
    vkDestroyShaderModule(Device, Vertex, nullptr);
    return Pipe;
  };
  // Writer: always passes, replaces stencil with 1, restricted to the left
  // half of the render area.
  VkPipeline Writer =
      makePipeline(RedFragmentSource, VkRect2D{{0, 0}, {Extent / 2, Extent}},
                   VK_COMPARE_OP_ALWAYS, VK_STENCIL_OP_REPLACE);
  // Tester: only passes where stencil already equals 1, covering the whole
  // render area.
  VkPipeline Tester =
      makePipeline(GreenFragmentSource, VkRect2D{{0, 0}, {Extent, Extent}},
                   VK_COMPARE_OP_EQUAL, VK_STENCIL_OP_KEEP);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValues[2]{};
  ClearValues[0].color = {{0.0f, 0.0f, 1.0f, 1.0f}};
  ClearValues[1].depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = LocalPass;
  PassBegin.framebuffer = LocalFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 2;
  PassBegin.pClearValues = ClearValues;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Writer);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Tester);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // Left half: the writer's stencil (1) matched the tester's reference, so
  // the tester's green landed. Right half: stencil stayed 0, so the tester
  // was rejected and the clear color (blue) survives.
  EXPECT_EQ(texel(0, 0)[1], 0xFF);
  EXPECT_EQ(texel(1, 3)[1], 0xFF);
  EXPECT_EQ(texel(2, 0)[2], 0xFF);
  EXPECT_EQ(texel(3, 3)[2], 0xFF);

  vkDestroyPipeline(Device, Tester, nullptr);
  vkDestroyPipeline(Device, Writer, nullptr);
  vkDestroyFramebuffer(Device, LocalFb, nullptr);
  vkDestroyRenderPass(Device, LocalPass, nullptr);
  vkDestroyImageView(Device, StencilView, nullptr);
  vkDestroyImage(Device, StencilImage, nullptr);
  vkFreeMemory(Device, StencilMemory, nullptr);
}

/// Roadmap C1 ("Mandatory formats"): a combined `D24_UNORM_S8_UINT`
/// attachment shares one word of storage between its depth and stencil
/// halves, so the completion scenario is that writing one half through
/// `vkCmdDraw` never corrupts the other. One draw enables both depth and
/// stencil testing/writes together (depth 0.2, stencil 1); a second,
/// depth-only draw at depth 0.8 must still fail (`LESS`: 0.8 is not less
/// than 0.2, so the depth half survived); a third, stencil-only draw
/// (`EQUAL` against 1) must still pass (so the stencil half survived the
/// first draw's depth write) and its green lands last.
TEST_F(DrawTest, RendersWithCombinedDepthStencilAttachment) {
  VkImage DepthStencilImage = VK_NULL_HANDLE;
  VkImageView DepthStencilView = VK_NULL_HANDLE;
  VkDeviceMemory DepthStencilMemory = VK_NULL_HANDLE;
  createImageAndView(VK_FORMAT_D24_UNORM_S8_UINT,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                     VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                     DepthStencilImage, DepthStencilView, DepthStencilMemory);

  VkAttachmentDescription Attachments[2]{};
  Attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  Attachments[1].format = VK_FORMAT_D24_UNORM_S8_UINT;
  Attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  Attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference DepthStencilRef{
      1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 1;
  Subpass.pColorAttachments = &ColorRef;
  Subpass.pDepthStencilAttachment = &DepthStencilRef;
  VkRenderPassCreateInfo PassInfo{};
  PassInfo.attachmentCount = 2;
  PassInfo.pAttachments = Attachments;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass LocalPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &LocalPass),
            VK_SUCCESS);

  VkImageView FbViews[2] = {ColorView, DepthStencilView};
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = LocalPass;
  FbInfo.attachmentCount = 2;
  FbInfo.pAttachments = FbViews;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer LocalFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &LocalFb),
            VK_SUCCESS);

  auto makePipeline =
      [&](llvm::StringRef VertexSource, llvm::StringRef FragmentSource,
          const VkPipelineDepthStencilStateCreateInfo &DepthStencil) {
        VkShaderModule Vertex = createModule(VertexSource);
        VkShaderModule Fragment = createModule(FragmentSource);
        VkPipelineShaderStageCreateInfo Stages[2]{};
        Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        Stages[0].module = Vertex;
        Stages[0].pName = "main";
        Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        Stages[1].module = Fragment;
        Stages[1].pName = "main";
        VkPipelineVertexInputStateCreateInfo VertexInput{};
        VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
        InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport Viewport{0.0f,          0.0f, float(Extent),
                            float(Extent), 0.0f, 1.0f};
        VkRect2D Scissor{{0, 0}, {Extent, Extent}};
        VkPipelineViewportStateCreateInfo ViewportState{};
        ViewportState.viewportCount = 1;
        ViewportState.pViewports = &Viewport;
        ViewportState.scissorCount = 1;
        ViewportState.pScissors = &Scissor;
        VkPipelineRasterizationStateCreateInfo Raster{};
        Raster.cullMode = VK_CULL_MODE_NONE;
        Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        Raster.polygonMode = VK_POLYGON_MODE_FILL;
        VkPipelineMultisampleStateCreateInfo Multisample{};
        Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState BlendAttachment{};
        BlendAttachment.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo Blend{};
        Blend.attachmentCount = 1;
        Blend.pAttachments = &BlendAttachment;
        VkPipelineDepthStencilStateCreateInfo LocalDepthStencil = DepthStencil;
        VkGraphicsPipelineCreateInfo Info{};
        Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        Info.stageCount = 2;
        Info.pStages = Stages;
        Info.pVertexInputState = &VertexInput;
        Info.pInputAssemblyState = &InputAssembly;
        Info.pViewportState = &ViewportState;
        Info.pRasterizationState = &Raster;
        Info.pMultisampleState = &Multisample;
        Info.pDepthStencilState = &LocalDepthStencil;
        Info.pColorBlendState = &Blend;
        Info.layout = Layout;
        Info.renderPass = LocalPass;
        VkPipeline Pipe = VK_NULL_HANDLE;
        EXPECT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info,
                                            nullptr, &Pipe),
                  VK_SUCCESS);
        vkDestroyShaderModule(Device, Fragment, nullptr);
        vkDestroyShaderModule(Device, Vertex, nullptr);
        return Pipe;
      };

  // Draw 1: writes depth (0.2, `ALWAYS`) and stencil (1, `REPLACE` on
  // `ALWAYS`) together, in one draw against the combined attachment.
  VkStencilOpState WriteFace{};
  WriteFace.failOp = VK_STENCIL_OP_KEEP;
  WriteFace.passOp = VK_STENCIL_OP_REPLACE;
  WriteFace.depthFailOp = VK_STENCIL_OP_KEEP;
  WriteFace.compareOp = VK_COMPARE_OP_ALWAYS;
  WriteFace.compareMask = 0xFF;
  WriteFace.writeMask = 0xFF;
  WriteFace.reference = 1;
  VkPipelineDepthStencilStateCreateInfo WriteState{};
  WriteState.depthTestEnable = VK_TRUE;
  WriteState.depthWriteEnable = VK_TRUE;
  WriteState.depthCompareOp = VK_COMPARE_OP_ALWAYS;
  WriteState.stencilTestEnable = VK_TRUE;
  WriteState.front = WriteFace;
  WriteState.back = WriteFace;
  VkPipeline Writer =
      makePipeline(NearDepthVertexSource, RedFragmentSource, WriteState);

  // Draw 2: depth-only (`LESS`), no stencil test -- must still fail since
  // 0.8 is not less than the depth draw 1 stored (0.2), proving the
  // stencil write did not corrupt the depth half of the shared word.
  VkPipelineDepthStencilStateCreateInfo DepthOnlyState{};
  DepthOnlyState.depthTestEnable = VK_TRUE;
  DepthOnlyState.depthWriteEnable = VK_TRUE;
  DepthOnlyState.depthCompareOp = VK_COMPARE_OP_LESS;
  VkPipeline DepthBlocked =
      makePipeline(FarDepthVertexSource, GreenFragmentSource, DepthOnlyState);

  // Draw 3: stencil-only (`EQUAL` against 1), no depth test -- must still
  // pass since draw 1 left stencil at 1, proving the depth write did not
  // corrupt the stencil half.
  VkStencilOpState TestFace{};
  TestFace.failOp = VK_STENCIL_OP_KEEP;
  TestFace.passOp = VK_STENCIL_OP_KEEP;
  TestFace.depthFailOp = VK_STENCIL_OP_KEEP;
  TestFace.compareOp = VK_COMPARE_OP_EQUAL;
  TestFace.compareMask = 0xFF;
  TestFace.writeMask = 0xFF;
  TestFace.reference = 1;
  VkPipelineDepthStencilStateCreateInfo StencilOnlyState{};
  StencilOnlyState.stencilTestEnable = VK_TRUE;
  StencilOnlyState.front = TestFace;
  StencilOnlyState.back = TestFace;
  VkPipeline StencilPassed = makePipeline(
      FullscreenVertexSource, GreenFragmentSource, StencilOnlyState);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValues[2]{};
  ClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  ClearValues[1].depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = LocalPass;
  PassBegin.framebuffer = LocalFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 2;
  PassBegin.pClearValues = ClearValues;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Writer);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, DepthBlocked);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, StencilPassed);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel = texel(X, Y);
      EXPECT_EQ(Texel[0], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[1], 0xFF) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, StencilPassed, nullptr);
  vkDestroyPipeline(Device, DepthBlocked, nullptr);
  vkDestroyPipeline(Device, Writer, nullptr);
  vkDestroyFramebuffer(Device, LocalFb, nullptr);
  vkDestroyRenderPass(Device, LocalPass, nullptr);
  vkDestroyImageView(Device, DepthStencilView, nullptr);
  vkDestroyImage(Device, DepthStencilImage, nullptr);
  vkFreeMemory(Device, DepthStencilMemory, nullptr);
}

/// `BlendState::BlendEnable`: a half-alpha fragment source-over-blends with
/// the attachment's existing (clear) color, rather than replacing it -- the
/// completion scenario's own "blending" bullet.
TEST_F(DrawTest, RendersWithAlphaBlending) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(HalfAlphaRedFragmentSource);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.blendEnable = VK_TRUE;
  BlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  BlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  BlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  BlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  BlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  BlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;
  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.layout = Layout;
  Info.renderPass = Pass;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 1.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // 0.5*red + 0.5*blue (clear) = (0.5, 0, 0.5); 0.5 rounds to 0x80 in
  // `R8G8B8A8_UNORM`.
  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel = texel(X, Y);
      EXPECT_EQ(Texel[0], 0x80) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[1], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[2], 0x80) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Dual-source blend factors (roadmap C4, `VK_BLEND_FACTOR_SRC1_*`): a
/// real SPIR-V fragment stage with an `Index=1` output at the same
/// `Location=0` as its ordinary one (`DualSourceFragmentSource`), whose
/// `Index` decoration survives `spirv` -> `llvm` conversion
/// (`feme::spirv::attachStageIODecorations`) and gets reflected into
/// `SignatureElement::Index` (`CanonicalizeStage.cpp`'s
/// `parseSPIRVDecorations`). `SrcColorFactor`/`SrcAlphaFactor` of
/// `Src1Color`/`Src1Alpha` with `DstColorFactor`/`DstAlphaFactor` of
/// `Zero` isolates the `Index=1` output's (0.25, 0.5, 0.75, 1.0) in the
/// result, exactly like
/// `ExecutorTest.DualSourceBlendReadsTheSecondFragmentOutput` but end to end
/// through real SPIR-V rather than a hand-built `EntrySignature`.
TEST_F(DrawTest, RendersWithDualSourceBlending) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(DualSourceFragmentSource);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.blendEnable = VK_TRUE;
  BlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC1_COLOR;
  BlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
  BlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  BlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC1_ALPHA;
  BlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  BlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;
  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.layout = Layout;
  Info.renderPass = Pass;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // result = 1*Src1Color + 0*DstColor = (0.25, 0.5, 0.75), rounding to
  // (0x40, 0x80, 0xBF) in `R8G8B8A8_UNORM`.
  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel = texel(X, Y);
      EXPECT_NEAR(Texel[0], 0x40, 2) << "at (" << X << ", " << Y << ")";
      EXPECT_NEAR(Texel[1], 0x80, 2) << "at (" << X << ", " << Y << ")";
      EXPECT_NEAR(Texel[2], 0xBF, 2) << "at (" << X << ", " << Y << ")";
      EXPECT_NEAR(Texel[3], 0xFF, 2) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Multiple color attachments: a fragment stage with two `Output`s writes
/// distinct colors to each, and both land in their own attachment -- the
/// completion scenario's own "MRT" bullet.
TEST_F(DrawTest, RendersToMultipleColorAttachments) {
  VkImage SecondImage = VK_NULL_HANDLE;
  VkImageView SecondView = VK_NULL_HANDLE;
  VkDeviceMemory SecondMemory = VK_NULL_HANDLE;
  createImageAndView(
      VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_COLOR_BIT, SecondImage, SecondView, SecondMemory);

  VkAttachmentDescription Attachments[2]{};
  for (VkAttachmentDescription &A : Attachments) {
    A.format = VK_FORMAT_R8G8B8A8_UNORM;
    A.samples = VK_SAMPLE_COUNT_1_BIT;
    A.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    A.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  }
  VkAttachmentReference ColorRefs[2] = {
      {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
      {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 2;
  Subpass.pColorAttachments = ColorRefs;
  VkRenderPassCreateInfo PassInfo{};
  PassInfo.attachmentCount = 2;
  PassInfo.pAttachments = Attachments;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass LocalPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &LocalPass),
            VK_SUCCESS);

  VkImageView FbViews[2] = {ColorView, SecondView};
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = LocalPass;
  FbInfo.attachmentCount = 2;
  FbInfo.pAttachments = FbViews;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer LocalFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &LocalFb),
            VK_SUCCESS);

  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(DualOutputFragmentSource);
  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachments[2]{};
  BlendAttachments[0].colorWriteMask = 0xF;
  BlendAttachments[1].colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 2;
  Blend.pAttachments = BlendAttachments;
  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.layout = Layout;
  Info.renderPass = LocalPass;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValues[2]{};
  ClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  ClearValues[1].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = LocalPass;
  PassBegin.framebuffer = LocalFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 2;
  PassBegin.pClearValues = ClearValues;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel0 = texelOf(ColorImage, X, Y);
      std::array<uint8_t, 4> Texel1 = texelOf(SecondImage, X, Y);
      EXPECT_EQ(Texel0[0], 0xFF) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel0[1], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel1[0], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel1[1], 0xFF) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyFramebuffer(Device, LocalFb, nullptr);
  vkDestroyRenderPass(Device, LocalPass, nullptr);
  vkDestroyImageView(Device, SecondView, nullptr);
  vkDestroyImage(Device, SecondImage, nullptr);
  vkFreeMemory(Device, SecondMemory, nullptr);
}

/// (roadmap F8) `vkCmdSetRenderingAttachmentLocations`: remapping location 0
/// onto attachment 1 and location 1 onto attachment 0 swaps which
/// attachment each fragment output lands in, relative to the identity
/// mapping `RendersToMultipleColorAttachments` (and every other MRT test)
/// exercises by omission.
TEST_F(DrawTest, RenderingAttachmentLocationsRemapsColorOutputs) {
  VkImage SecondImage = VK_NULL_HANDLE;
  VkImageView SecondView = VK_NULL_HANDLE;
  VkDeviceMemory SecondMemory = VK_NULL_HANDLE;
  createImageAndView(
      VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_COLOR_BIT, SecondImage, SecondView, SecondMemory);

  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(DualOutputFragmentSource);

  VkFormat ColorFormats[2] = {VK_FORMAT_R8G8B8A8_UNORM,
                              VK_FORMAT_R8G8B8A8_UNORM};
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 2;
  Rendering.pColorAttachmentFormats = ColorFormats;

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachments[2]{};
  BlendAttachments[0].colorWriteMask = 0xF;
  BlendAttachments[1].colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 2;
  Blend.pAttachments = BlendAttachments;
  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.layout = Layout;
  Info.pNext = &Rendering;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);

  VkRenderingAttachmentInfo ColorAttachments[2]{};
  ColorAttachments[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  ColorAttachments[0].imageView = ColorView;
  ColorAttachments[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  ColorAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  ColorAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  ColorAttachments[0].clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  ColorAttachments[1].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  ColorAttachments[1].imageView = SecondView;
  ColorAttachments[1].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  ColorAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  ColorAttachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  ColorAttachments[1].clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

  VkRenderingInfo RenderingInfo{};
  RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  RenderingInfo.renderArea = {{0, 0}, {Extent, Extent}};
  RenderingInfo.layerCount = 1;
  RenderingInfo.colorAttachmentCount = 2;
  RenderingInfo.pColorAttachments = ColorAttachments;

  vkCmdBeginRenderingKHR(Cmd, &RenderingInfo);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  // Swap: location 0 (solid red) now writes attachment 1, location 1
  // (solid green) now writes attachment 0.
  uint32_t Locations[2] = {1, 0};
  VkRenderingAttachmentLocationInfo LocationInfo{};
  LocationInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_LOCATION_INFO;
  LocationInfo.colorAttachmentCount = 2;
  LocationInfo.pColorAttachmentLocations = Locations;
  vkCmdSetRenderingAttachmentLocations(Cmd, &LocationInfo);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderingKHR(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel0 = texelOf(ColorImage, X, Y);
      std::array<uint8_t, 4> Texel1 = texelOf(SecondImage, X, Y);
      // Attachment 0 now holds location 1's solid green; attachment 1
      // holds location 0's solid red -- swapped relative to the identity
      // mapping.
      EXPECT_EQ(Texel0[0], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel0[1], 0xFF) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel1[0], 0xFF) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel1[1], 0x00) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyImageView(Device, SecondView, nullptr);
  vkDestroyImage(Device, SecondImage, nullptr);
  vkFreeMemory(Device, SecondMemory, nullptr);
}

/// (roadmap F8) `VK_ATTACHMENT_UNUSED` in `pColorAttachmentLocations`
/// discards whatever the fragment shader wrote to that location: attachment
/// 1 keeps its clear color rather than receiving location 1's write, since
/// no location is mapped onto it.
TEST_F(DrawTest, RenderingAttachmentLocationsUnusedDiscardsWrite) {
  VkImage SecondImage = VK_NULL_HANDLE;
  VkImageView SecondView = VK_NULL_HANDLE;
  VkDeviceMemory SecondMemory = VK_NULL_HANDLE;
  createImageAndView(
      VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_COLOR_BIT, SecondImage, SecondView, SecondMemory);

  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(DualOutputFragmentSource);

  VkFormat ColorFormats[2] = {VK_FORMAT_R8G8B8A8_UNORM,
                              VK_FORMAT_R8G8B8A8_UNORM};
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 2;
  Rendering.pColorAttachmentFormats = ColorFormats;

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachments[2]{};
  BlendAttachments[0].colorWriteMask = 0xF;
  BlendAttachments[1].colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 2;
  Blend.pAttachments = BlendAttachments;
  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.layout = Layout;
  Info.pNext = &Rendering;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);

  VkRenderingAttachmentInfo ColorAttachments[2]{};
  ColorAttachments[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  ColorAttachments[0].imageView = ColorView;
  ColorAttachments[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  ColorAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  ColorAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  ColorAttachments[0].clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  ColorAttachments[1].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  ColorAttachments[1].imageView = SecondView;
  ColorAttachments[1].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  ColorAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  ColorAttachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  // A distinctive clear color (not black, not either shader output) so a
  // surviving clear is unambiguous.
  ColorAttachments[1].clearValue.color = {{0.2f, 0.2f, 0.6f, 1.0f}};

  VkRenderingInfo RenderingInfo{};
  RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  RenderingInfo.renderArea = {{0, 0}, {Extent, Extent}};
  RenderingInfo.layerCount = 1;
  RenderingInfo.colorAttachmentCount = 2;
  RenderingInfo.pColorAttachments = ColorAttachments;

  vkCmdBeginRenderingKHR(Cmd, &RenderingInfo);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  // Location 0 still writes attachment 0; location 1 maps nowhere
  // (`VK_ATTACHMENT_UNUSED`), so attachment 1 keeps its clear color.
  uint32_t Locations[2] = {0, VK_ATTACHMENT_UNUSED};
  VkRenderingAttachmentLocationInfo LocationInfo{};
  LocationInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_LOCATION_INFO;
  LocationInfo.colorAttachmentCount = 2;
  LocationInfo.pColorAttachmentLocations = Locations;
  vkCmdSetRenderingAttachmentLocations(Cmd, &LocationInfo);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderingKHR(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel0 = texelOf(ColorImage, X, Y);
      std::array<uint8_t, 4> Texel1 = texelOf(SecondImage, X, Y);
      EXPECT_EQ(Texel0[0], 0xFF) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel0[1], 0x00) << "at (" << X << ", " << Y << ")";
      // Attachment 1's clear color (0.2, 0.2, 0.6) survives, in
      // R8G8B8A8_UNORM: (0x33, 0x33, 0x99).
      EXPECT_NEAR(Texel1[0], 0x33, 2) << "at (" << X << ", " << Y << ")";
      EXPECT_NEAR(Texel1[1], 0x33, 2) << "at (" << X << ", " << Y << ")";
      EXPECT_NEAR(Texel1[2], 0x99, 2) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyImageView(Device, SecondView, nullptr);
  vkDestroyImage(Device, SecondImage, nullptr);
  vkFreeMemory(Device, SecondMemory, nullptr);
}

/// (roadmap F8) `vkCmdSetRenderingAttachmentLocations`/`vkCmdSetRendering
/// InputAttachmentIndices` are only valid inside a `vkCmdBeginRendering`
/// instance -- a classic `VkRenderPass`'s attachment/location
/// correspondence is fixed by its `VkSubpassDescription`, which the
/// extension does not touch.
TEST_F(DrawTest, RenderingAttachmentLocationsRejectedOutsideDynamicRendering) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  uint32_t Locations[1] = {0};
  VkRenderingAttachmentLocationInfo LocationInfo{};
  LocationInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_LOCATION_INFO;
  LocationInfo.colorAttachmentCount = 1;
  LocationInfo.pColorAttachmentLocations = Locations;
  vkCmdSetRenderingAttachmentLocations(Cmd, &LocationInfo);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  EXPECT_EQ(submit(), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap F8) `pColorAttachmentLocations`'s `colorAttachmentCount` must
/// agree with the current dynamic-rendering instance's own color
/// attachment count.
TEST_F(DrawTest, RenderingAttachmentLocationsRejectsMismatchedCount) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkFormat ColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 1;
  Rendering.pColorAttachmentFormats = &ColorFormat;
  VkPipeline Pipe = createPipeline(Vertex, Fragment, &Rendering);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkRenderingAttachmentInfo ColorAttachment{};
  ColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  ColorAttachment.imageView = ColorView;
  ColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkRenderingInfo RenderingInfo{};
  RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  RenderingInfo.renderArea = {{0, 0}, {Extent, Extent}};
  RenderingInfo.layerCount = 1;
  RenderingInfo.colorAttachmentCount = 1;
  RenderingInfo.pColorAttachments = &ColorAttachment;

  vkCmdBeginRenderingKHR(Cmd, &RenderingInfo);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  // Two entries for a one-color-attachment rendering instance.
  uint32_t Locations[2] = {0, 1};
  VkRenderingAttachmentLocationInfo LocationInfo{};
  LocationInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_LOCATION_INFO;
  LocationInfo.colorAttachmentCount = 2;
  LocationInfo.pColorAttachmentLocations = Locations;
  vkCmdSetRenderingAttachmentLocations(Cmd, &LocationInfo);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderingKHR(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  EXPECT_EQ(submit(), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap F8) Two locations may never map onto the same color attachment:
/// which fragment output would win is undefined, so this is rejected
/// rather than silently resolved by picking one.
TEST_F(DrawTest, RenderingAttachmentLocationsRejectsDuplicateMapping) {
  VkImage SecondImage = VK_NULL_HANDLE;
  VkImageView SecondView = VK_NULL_HANDLE;
  VkDeviceMemory SecondMemory = VK_NULL_HANDLE;
  createImageAndView(
      VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_COLOR_BIT, SecondImage, SecondView, SecondMemory);

  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(DualOutputFragmentSource);
  VkFormat ColorFormats[2] = {VK_FORMAT_R8G8B8A8_UNORM,
                              VK_FORMAT_R8G8B8A8_UNORM};
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 2;
  Rendering.pColorAttachmentFormats = ColorFormats;

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachments[2]{};
  BlendAttachments[0].colorWriteMask = 0xF;
  BlendAttachments[1].colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 2;
  Blend.pAttachments = BlendAttachments;
  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.layout = Layout;
  Info.pNext = &Rendering;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkRenderingAttachmentInfo ColorAttachments[2]{};
  ColorAttachments[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  ColorAttachments[0].imageView = ColorView;
  ColorAttachments[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  ColorAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  ColorAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  ColorAttachments[1].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  ColorAttachments[1].imageView = SecondView;
  ColorAttachments[1].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  ColorAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  ColorAttachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkRenderingInfo RenderingInfo{};
  RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  RenderingInfo.renderArea = {{0, 0}, {Extent, Extent}};
  RenderingInfo.layerCount = 1;
  RenderingInfo.colorAttachmentCount = 2;
  RenderingInfo.pColorAttachments = ColorAttachments;

  vkCmdBeginRenderingKHR(Cmd, &RenderingInfo);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  // Both locations claim attachment 0.
  uint32_t Locations[2] = {0, 0};
  VkRenderingAttachmentLocationInfo LocationInfo{};
  LocationInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_LOCATION_INFO;
  LocationInfo.colorAttachmentCount = 2;
  LocationInfo.pColorAttachmentLocations = Locations;
  vkCmdSetRenderingAttachmentLocations(Cmd, &LocationInfo);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderingKHR(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  EXPECT_EQ(submit(), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyImageView(Device, SecondView, nullptr);
  vkDestroyImage(Device, SecondImage, nullptr);
  vkFreeMemory(Device, SecondMemory, nullptr);
}

/// (roadmap F8) `vkCmdSetRenderingInputAttachmentIndices`'s state is
/// recorded and validated the same way (dynamic-rendering-only,
/// `colorAttachmentCount` must agree); it has no observable effect yet
/// since no shader-side `subpassInput` local-read consumer exists (a
/// separate, larger gap FeMeVulkanDesign.md's "Render passes and dynamic
/// rendering" section documents), but a well-formed call must not fail
/// the draw it precedes.
TEST_F(DrawTest, RenderingInputAttachmentIndicesIsRecordedWithoutError) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkFormat ColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 1;
  Rendering.pColorAttachmentFormats = &ColorFormat;
  VkPipeline Pipe = createPipeline(Vertex, Fragment, &Rendering);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkRenderingAttachmentInfo ColorAttachment{};
  ColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  ColorAttachment.imageView = ColorView;
  ColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkRenderingInfo RenderingInfo{};
  RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  RenderingInfo.renderArea = {{0, 0}, {Extent, Extent}};
  RenderingInfo.layerCount = 1;
  RenderingInfo.colorAttachmentCount = 1;
  RenderingInfo.pColorAttachments = &ColorAttachment;

  vkCmdBeginRenderingKHR(Cmd, &RenderingInfo);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  uint32_t ColorIndices[1] = {0};
  uint32_t DepthIndex = 1;
  VkRenderingInputAttachmentIndexInfo IndexInfo{};
  IndexInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INPUT_ATTACHMENT_INDEX_INFO;
  IndexInfo.colorAttachmentCount = 1;
  IndexInfo.pColorAttachmentInputIndices = ColorIndices;
  IndexInfo.pDepthInputAttachmentIndex = &DepthIndex;
  vkCmdSetRenderingInputAttachmentIndices(Cmd, &IndexInfo);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderingKHR(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  EXPECT_EQ(submit(), VK_SUCCESS);
  EXPECT_EQ(texel(0, 0)[0], 0xFF);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

TEST_F(DrawTest, OcclusionQueryCountsPassedSamples) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipeline Pipe = createPipeline(Vertex, Fragment);

  VkQueryPoolCreateInfo QueryInfo{};
  QueryInfo.queryType = VK_QUERY_TYPE_OCCLUSION;
  QueryInfo.queryCount = 1;
  VkQueryPool QueryPool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateQueryPool(Device, &QueryInfo, nullptr, &QueryPool),
            VK_SUCCESS);

  beginRenderPass({{0.0f, 0.0f, 0.0f, 1.0f}});
  vkCmdResetQueryPool(Cmd, QueryPool, 0, 1);
  vkCmdBeginQuery(Cmd, QueryPool, 0, 0);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndQuery(Cmd, QueryPool, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  uint64_t Results[2] = {0, 0};
  EXPECT_EQ(vkGetQueryPoolResults(Device, QueryPool, 0, 1, sizeof(Results),
                                  Results, 2 * sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT |
                                      VK_QUERY_RESULT_WITH_AVAILABILITY_BIT),
            VK_SUCCESS);
  EXPECT_EQ(Results[0], uint64_t(Extent * Extent));
  EXPECT_EQ(Results[1], 1u);

  vkDestroyQueryPool(Device, QueryPool, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// A multisample color attachment with a resolve attachment: a draw fully
/// covering the render area resolves to a uniform color in the
/// single-sample target -- the completion scenario's own "multisample
/// resolves" bullet.
TEST_F(DrawTest, ResolvesMultisampleColorDuringRenderPass) {
  VkImage MSImage = VK_NULL_HANDLE;
  VkImageView MSView = VK_NULL_HANDLE;
  VkDeviceMemory MSMemory = VK_NULL_HANDLE;
  createImageAndView(VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT, MSImage, MSView, MSMemory,
                     VK_SAMPLE_COUNT_4_BIT);
  VkImage ResolveImage = VK_NULL_HANDLE;
  VkImageView ResolveView = VK_NULL_HANDLE;
  VkDeviceMemory ResolveMemory = VK_NULL_HANDLE;
  createImageAndView(
      VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_COLOR_BIT, ResolveImage, ResolveView, ResolveMemory);

  VkAttachmentDescription Attachments[2]{};
  Attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachments[0].samples = VK_SAMPLE_COUNT_4_BIT;
  Attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  Attachments[1].format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference ResolveRef{1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 1;
  Subpass.pColorAttachments = &ColorRef;
  Subpass.pResolveAttachments = &ResolveRef;
  VkRenderPassCreateInfo PassInfo{};
  PassInfo.attachmentCount = 2;
  PassInfo.pAttachments = Attachments;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass LocalPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &LocalPass),
            VK_SUCCESS);

  VkImageView FbViews[2] = {MSView, ResolveView};
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = LocalPass;
  FbInfo.attachmentCount = 2;
  FbInfo.pAttachments = FbViews;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer LocalFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &LocalFb),
            VK_SUCCESS);

  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);
  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;
  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.layout = Layout;
  Info.renderPass = LocalPass;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValues[2]{};
  ClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  ClearValues[1].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = LocalPass;
  PassBegin.framebuffer = LocalFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 2;
  PassBegin.pClearValues = ClearValues;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // Every sample of every covered pixel is the same solid red, so the
  // resolve target's box-filtered average is exactly red too.
  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel = texelOf(ResolveImage, X, Y);
      EXPECT_EQ(Texel[0], 0xFF) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[1], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[3], 0xFF) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyFramebuffer(Device, LocalFb, nullptr);
  vkDestroyRenderPass(Device, LocalPass, nullptr);
  vkDestroyImageView(Device, ResolveView, nullptr);
  vkDestroyImage(Device, ResolveImage, nullptr);
  vkFreeMemory(Device, ResolveMemory, nullptr);
  vkDestroyImageView(Device, MSView, nullptr);
  vkDestroyImage(Device, MSImage, nullptr);
  vkFreeMemory(Device, MSMemory, nullptr);
}

/// (Roadmap F8a) The shader-side half of `VK_KHR_dynamic_rendering_local_
/// read` this milestone closes out: a first draw fills the attachment
/// solid red (`RedFragmentSource`); without ending the rendering instance,
/// a second draw's fragment shader (`SubpassLoadFragmentSource`) reads that
/// same attachment back through a `subpassInput` local read and turns it
/// solid green -- proving a real pixel round-trips through
/// `feme.stage.subpass.load` end to end, not just that the two `vkCmdSet
/// Rendering{AttachmentLocations,InputAttachmentIndices}` commands are
/// accepted (see this file's own `RenderingAttachmentLocationsRemapsColor
/// Outputs` and CommandBufferTest's F8 validation coverage).
TEST_F(DrawTest, SubpassLoadReadsBackTheColorAttachmentItWrote) {
  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule RedFragment = createModule(RedFragmentSource);
  VkShaderModule SubpassFragment = createModule(SubpassLoadFragmentSource);

  // The subpass shader's own `bind(0, 0)` needs a matching descriptor set
  // layout entry to compile against, even though roadmap F8a's read never
  // consults whatever gets bound there at draw time (see
  // SPIRVToLLVMPatterns.cpp's `SubpassLoadPattern`): it is not a
  // descriptor-set image.
  VkDescriptorSetLayoutBinding Binding{};
  Binding.binding = 0;
  Binding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  Binding.descriptorCount = 1;
  Binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
  SetLayoutInfo.bindingCount = 1;
  SetLayoutInfo.pBindings = &Binding;
  VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreateDescriptorSetLayout(Device, &SetLayoutInfo, nullptr, &SetLayout),
      VK_SUCCESS);
  VkPipelineLayoutCreateInfo SubpassLayoutInfo{};
  SubpassLayoutInfo.setLayoutCount = 1;
  SubpassLayoutInfo.pSetLayouts = &SetLayout;
  VkPipelineLayout SubpassLayout = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineLayout(Device, &SubpassLayoutInfo, nullptr,
                                   &SubpassLayout),
            VK_SUCCESS);

  VkDescriptorPoolSize PoolSize{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1};
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
  VkDescriptorImageInfo ImageInfo{};
  ImageInfo.imageView = ColorView;
  ImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  VkWriteDescriptorSet Write{};
  Write.dstSet = Set;
  Write.dstBinding = 0;
  Write.descriptorCount = 1;
  Write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  Write.pImageInfo = &ImageInfo;
  vkUpdateDescriptorSets(Device, 1, &Write, 0, nullptr);

  VkFormat ColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 1;
  Rendering.pColorAttachmentFormats = &ColorFormat;

  VkPipeline RedPipe = createPipeline(Vertex, RedFragment, &Rendering);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = SubpassFragment;
  Stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;
  VkGraphicsPipelineCreateInfo SubpassInfo{};
  SubpassInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  SubpassInfo.stageCount = 2;
  SubpassInfo.pStages = Stages;
  SubpassInfo.pVertexInputState = &VertexInput;
  SubpassInfo.pInputAssemblyState = &InputAssembly;
  SubpassInfo.pViewportState = &ViewportState;
  SubpassInfo.pRasterizationState = &Raster;
  SubpassInfo.pMultisampleState = &Multisample;
  SubpassInfo.pColorBlendState = &Blend;
  SubpassInfo.layout = SubpassLayout;
  SubpassInfo.pNext = &Rendering;
  VkPipeline SubpassPipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &SubpassInfo,
                                      nullptr, &SubpassPipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);

  VkRenderingAttachmentInfo ColorAttachment{};
  ColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  ColorAttachment.imageView = ColorView;
  ColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  ColorAttachment.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

  VkRenderingInfo RenderingInfo{};
  RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  RenderingInfo.renderArea = {{0, 0}, {Extent, Extent}};
  RenderingInfo.layerCount = 1;
  RenderingInfo.colorAttachmentCount = 1;
  RenderingInfo.pColorAttachments = &ColorAttachment;

  vkCmdBeginRenderingKHR(Cmd, &RenderingInfo);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, RedPipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);

  // Explicit identity mapping (attachment 0 -> input-attachment index 0),
  // exercising the command rather than relying on its own default.
  uint32_t ColorIndices[1] = {0};
  VkRenderingInputAttachmentIndexInfo IndexInfo{};
  IndexInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INPUT_ATTACHMENT_INDEX_INFO;
  IndexInfo.colorAttachmentCount = 1;
  IndexInfo.pColorAttachmentInputIndices = ColorIndices;
  vkCmdSetRenderingInputAttachmentIndices(Cmd, &IndexInfo);

  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, SubpassPipe);
  vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, SubpassLayout,
                          0, 1, &Set, 0, nullptr);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderingKHR(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel = texel(X, Y);
      EXPECT_EQ(Texel[0], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[1], 0xFF) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[2], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[3], 0xFF) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, RedPipe, nullptr);
  vkDestroyPipeline(Device, SubpassPipe, nullptr);
  vkDestroyShaderModule(Device, SubpassFragment, nullptr);
  vkDestroyShaderModule(Device, RedFragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyDescriptorPool(Device, DescPool, nullptr);
  vkDestroyPipelineLayout(Device, SubpassLayout, nullptr);
  vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
}

/// (Roadmap F8b) The depth-attachment counterpart of the color test above:
/// a `D32_FLOAT` depth attachment is cleared to a known, exactly
/// representable value (`128.0 / 255.0`, so this format's identity
/// float-to-float decode round-trips it exactly), and one draw's fragment
/// shader (`SubpassLoadDepthFragmentSource`) reads it back through a
/// `subpassLoad` local read and writes it into the color attachment's
/// green channel -- proving `buildSubpassInputHeap` (CommandBuffer.cpp)
/// and the CPU runtime's now-decoded `D32_FLOAT` format (FeMeRuntimeCPU.c)
/// carry a real depth texel all the way to a pixel, not just that the two
/// `vkCmdSetRendering*` commands are accepted.
TEST_F(DrawTest, SubpassLoadReadsBackTheDepthAttachmentItWrote) {
  VkImage DepthImage = VK_NULL_HANDLE;
  VkImageView DepthView = VK_NULL_HANDLE;
  VkDeviceMemory DepthMemory = VK_NULL_HANDLE;
  createImageAndView(
      VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_DEPTH_BIT, DepthImage, DepthView, DepthMemory);

  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule SubpassFragment = createModule(SubpassLoadDepthFragmentSource);

  VkDescriptorSetLayoutBinding Binding{};
  Binding.binding = 0;
  Binding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  Binding.descriptorCount = 1;
  Binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
  SetLayoutInfo.bindingCount = 1;
  SetLayoutInfo.pBindings = &Binding;
  VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreateDescriptorSetLayout(Device, &SetLayoutInfo, nullptr, &SetLayout),
      VK_SUCCESS);
  VkPipelineLayoutCreateInfo SubpassLayoutInfo{};
  SubpassLayoutInfo.setLayoutCount = 1;
  SubpassLayoutInfo.pSetLayouts = &SetLayout;
  VkPipelineLayout SubpassLayout = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineLayout(Device, &SubpassLayoutInfo, nullptr,
                                   &SubpassLayout),
            VK_SUCCESS);

  VkDescriptorPoolSize PoolSize{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1};
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
  VkDescriptorImageInfo ImageInfo{};
  ImageInfo.imageView = DepthView;
  ImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  VkWriteDescriptorSet Write{};
  Write.dstSet = Set;
  Write.dstBinding = 0;
  Write.descriptorCount = 1;
  Write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  Write.pImageInfo = &ImageInfo;
  vkUpdateDescriptorSets(Device, 1, &Write, 0, nullptr);

  VkFormat ColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 1;
  Rendering.pColorAttachmentFormats = &ColorFormat;

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = SubpassFragment;
  Stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;
  VkGraphicsPipelineCreateInfo SubpassInfo{};
  SubpassInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  SubpassInfo.stageCount = 2;
  SubpassInfo.pStages = Stages;
  SubpassInfo.pVertexInputState = &VertexInput;
  SubpassInfo.pInputAssemblyState = &InputAssembly;
  SubpassInfo.pViewportState = &ViewportState;
  SubpassInfo.pRasterizationState = &Raster;
  SubpassInfo.pMultisampleState = &Multisample;
  SubpassInfo.pColorBlendState = &Blend;
  SubpassInfo.layout = SubpassLayout;
  SubpassInfo.pNext = &Rendering;
  VkPipeline SubpassPipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &SubpassInfo,
                                      nullptr, &SubpassPipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);

  VkRenderingAttachmentInfo ColorAttachment{};
  ColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  ColorAttachment.imageView = ColorView;
  ColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  ColorAttachment.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

  VkRenderingAttachmentInfo DepthAttachment{};
  DepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  DepthAttachment.imageView = DepthView;
  DepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  DepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  DepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  DepthAttachment.clearValue.depthStencil.depth = 128.0f / 255.0f;

  VkRenderingInfo RenderingInfo{};
  RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  RenderingInfo.renderArea = {{0, 0}, {Extent, Extent}};
  RenderingInfo.layerCount = 1;
  RenderingInfo.colorAttachmentCount = 1;
  RenderingInfo.pColorAttachments = &ColorAttachment;
  RenderingInfo.pDepthAttachment = &DepthAttachment;

  vkCmdBeginRenderingKHR(Cmd, &RenderingInfo);

  uint32_t DepthIndex = 0;
  VkRenderingInputAttachmentIndexInfo IndexInfo{};
  IndexInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INPUT_ATTACHMENT_INDEX_INFO;
  IndexInfo.pDepthInputAttachmentIndex = &DepthIndex;
  vkCmdSetRenderingInputAttachmentIndices(Cmd, &IndexInfo);

  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, SubpassPipe);
  vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, SubpassLayout,
                          0, 1, &Set, 0, nullptr);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderingKHR(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel = texel(X, Y);
      EXPECT_EQ(Texel[0], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[1], 128) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[2], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[3], 0xFF) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, SubpassPipe, nullptr);
  vkDestroyShaderModule(Device, SubpassFragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyDescriptorPool(Device, DescPool, nullptr);
  vkDestroyPipelineLayout(Device, SubpassLayout, nullptr);
  vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
  vkDestroyImageView(Device, DepthView, nullptr);
  vkDestroyImage(Device, DepthImage, nullptr);
  vkFreeMemory(Device, DepthMemory, nullptr);
}

/// (Roadmap F8b) The stencil-attachment counterpart: an `S8_UINT`
/// attachment is cleared to reference value 200 (chosen so `200 / 255.0`
/// normalized and re-scaled by the color store's own `[0, 255]` rounding
/// reproduces exactly 200, avoiding any double-rounding ambiguity), read
/// back through `subpassLoad` (`SubpassLoadStencilFragmentSource`) into
/// the color attachment's green channel.
TEST_F(DrawTest, SubpassLoadReadsBackTheStencilAttachmentItWrote) {
  VkImage StencilImage = VK_NULL_HANDLE;
  VkImageView StencilView = VK_NULL_HANDLE;
  VkDeviceMemory StencilMemory = VK_NULL_HANDLE;
  createImageAndView(
      VK_FORMAT_S8_UINT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_STENCIL_BIT, StencilImage, StencilView, StencilMemory);

  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule SubpassFragment =
      createModule(SubpassLoadStencilFragmentSource);

  VkDescriptorSetLayoutBinding Binding{};
  Binding.binding = 0;
  Binding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  Binding.descriptorCount = 1;
  Binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
  SetLayoutInfo.bindingCount = 1;
  SetLayoutInfo.pBindings = &Binding;
  VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreateDescriptorSetLayout(Device, &SetLayoutInfo, nullptr, &SetLayout),
      VK_SUCCESS);
  VkPipelineLayoutCreateInfo SubpassLayoutInfo{};
  SubpassLayoutInfo.setLayoutCount = 1;
  SubpassLayoutInfo.pSetLayouts = &SetLayout;
  VkPipelineLayout SubpassLayout = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineLayout(Device, &SubpassLayoutInfo, nullptr,
                                   &SubpassLayout),
            VK_SUCCESS);

  VkDescriptorPoolSize PoolSize{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1};
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
  VkDescriptorImageInfo ImageInfo{};
  ImageInfo.imageView = StencilView;
  ImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  VkWriteDescriptorSet Write{};
  Write.dstSet = Set;
  Write.dstBinding = 0;
  Write.descriptorCount = 1;
  Write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  Write.pImageInfo = &ImageInfo;
  vkUpdateDescriptorSets(Device, 1, &Write, 0, nullptr);

  VkFormat ColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 1;
  Rendering.pColorAttachmentFormats = &ColorFormat;

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = SubpassFragment;
  Stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;
  VkGraphicsPipelineCreateInfo SubpassInfo{};
  SubpassInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  SubpassInfo.stageCount = 2;
  SubpassInfo.pStages = Stages;
  SubpassInfo.pVertexInputState = &VertexInput;
  SubpassInfo.pInputAssemblyState = &InputAssembly;
  SubpassInfo.pViewportState = &ViewportState;
  SubpassInfo.pRasterizationState = &Raster;
  SubpassInfo.pMultisampleState = &Multisample;
  SubpassInfo.pColorBlendState = &Blend;
  SubpassInfo.layout = SubpassLayout;
  SubpassInfo.pNext = &Rendering;
  VkPipeline SubpassPipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &SubpassInfo,
                                      nullptr, &SubpassPipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);

  VkRenderingAttachmentInfo ColorAttachment{};
  ColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  ColorAttachment.imageView = ColorView;
  ColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  ColorAttachment.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

  VkRenderingAttachmentInfo StencilAttachment{};
  StencilAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  StencilAttachment.imageView = StencilView;
  StencilAttachment.imageLayout = VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL;
  StencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  StencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  StencilAttachment.clearValue.depthStencil.stencil = 200;

  VkRenderingInfo RenderingInfo{};
  RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  RenderingInfo.renderArea = {{0, 0}, {Extent, Extent}};
  RenderingInfo.layerCount = 1;
  RenderingInfo.colorAttachmentCount = 1;
  RenderingInfo.pColorAttachments = &ColorAttachment;
  RenderingInfo.pStencilAttachment = &StencilAttachment;

  vkCmdBeginRenderingKHR(Cmd, &RenderingInfo);

  uint32_t StencilIndex = 0;
  VkRenderingInputAttachmentIndexInfo IndexInfo{};
  IndexInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INPUT_ATTACHMENT_INDEX_INFO;
  IndexInfo.pStencilInputAttachmentIndex = &StencilIndex;
  vkCmdSetRenderingInputAttachmentIndices(Cmd, &IndexInfo);

  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, SubpassPipe);
  vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, SubpassLayout,
                          0, 1, &Set, 0, nullptr);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderingKHR(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      std::array<uint8_t, 4> Texel = texel(X, Y);
      EXPECT_EQ(Texel[0], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[1], 200) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[2], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Texel[3], 0xFF) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, SubpassPipe, nullptr);
  vkDestroyShaderModule(Device, SubpassFragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyDescriptorPool(Device, DescPool, nullptr);
  vkDestroyPipelineLayout(Device, SubpassLayout, nullptr);
  vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
  vkDestroyImageView(Device, StencilView, nullptr);
  vkDestroyImage(Device, StencilImage, nullptr);
  vkFreeMemory(Device, StencilMemory, nullptr);
}

/// (Roadmap F8c) The last piece `dynamicRenderingLocalReadMultisampled
/// Attachments` needed: a multisample color attachment's own samples are
/// seeded with 4 distinct, known values directly through its backing
/// memory (`ResolvesMultisampleColorDuringRenderPass`'s own multisample
/// attachment is written by a draw instead, but every covered sample of one
/// pixel always ends up identical there -- see that test's own comment --
/// so a draw can never produce 4 *different* per-sample values to read
/// back distinctly; direct memory seeding is the only way to construct
/// that shape). One draw's fragment shader
/// (`SubpassLoadMultisampleFragmentSource`) then reads sample 2 specifically
/// back through `subpassLoad`'s explicit-sample form and writes its red
/// channel into green -- proving a real, non-zero sample selects the
/// texel address `SubpassLoadReadsBackTheColorAttachmentItWrote`'s
/// (implicit, always-sample-0) form never had to. The same draw's own
/// (uniform, no-per-sample-shading) output write lands identically in
/// every sample of the same attachment (matching
/// `ResolvesMultisampleColorDuringRenderPass`'s own note), so reading back
/// any one sample after the draw recovers the shader's result.
TEST_F(DrawTest,
       SubpassLoadReadsBackAnExplicitSampleOfTheColorAttachmentItWrote) {
  constexpr uint32_t SampleCount = 4;
  VkImage MSImage = VK_NULL_HANDLE;
  VkImageView MSView = VK_NULL_HANDLE;
  VkDeviceMemory MSMemory = VK_NULL_HANDLE;
  createImageAndView(VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT, MSImage, MSView, MSMemory,
                     VK_SAMPLE_COUNT_4_BIT);

  // Every texel's 4 samples are seeded (0, 0, 0, 255), (64, 0, 0, 255),
  // (128, 0, 0, 255), (192, 0, 0, 255) -- sample 2's red channel is
  // therefore 128 everywhere, the value the fragment shader below is
  // expected to read back and re-store as green.
  std::vector<uint8_t> Seed(Extent * Extent * SampleCount * 4);
  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X)
      for (uint32_t S = 0; S != SampleCount; ++S) {
        size_t Off = ((size_t)(Y * Extent + X) * SampleCount + S) * 4;
        Seed[Off + 0] = static_cast<uint8_t>(S * 64);
        Seed[Off + 1] = 0;
        Seed[Off + 2] = 0;
        Seed[Off + 3] = 0xFF;
      }
  std::memcpy(fromHandle<Image>(MSImage)->data(), Seed.data(), Seed.size());

  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule SubpassFragment =
      createModule(SubpassLoadMultisampleFragmentSource);

  VkDescriptorSetLayoutBinding Binding{};
  Binding.binding = 0;
  Binding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  Binding.descriptorCount = 1;
  Binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
  SetLayoutInfo.bindingCount = 1;
  SetLayoutInfo.pBindings = &Binding;
  VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreateDescriptorSetLayout(Device, &SetLayoutInfo, nullptr, &SetLayout),
      VK_SUCCESS);
  VkPipelineLayoutCreateInfo SubpassLayoutInfo{};
  SubpassLayoutInfo.setLayoutCount = 1;
  SubpassLayoutInfo.pSetLayouts = &SetLayout;
  VkPipelineLayout SubpassLayout = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineLayout(Device, &SubpassLayoutInfo, nullptr,
                                   &SubpassLayout),
            VK_SUCCESS);

  VkDescriptorPoolSize PoolSize{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1};
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
  VkDescriptorImageInfo ImageInfo{};
  ImageInfo.imageView = MSView;
  ImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  VkWriteDescriptorSet Write{};
  Write.dstSet = Set;
  Write.dstBinding = 0;
  Write.descriptorCount = 1;
  Write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  Write.pImageInfo = &ImageInfo;
  vkUpdateDescriptorSets(Device, 1, &Write, 0, nullptr);

  VkFormat ColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 1;
  Rendering.pColorAttachmentFormats = &ColorFormat;

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = SubpassFragment;
  Stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;
  VkGraphicsPipelineCreateInfo SubpassInfo{};
  SubpassInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  SubpassInfo.stageCount = 2;
  SubpassInfo.pStages = Stages;
  SubpassInfo.pVertexInputState = &VertexInput;
  SubpassInfo.pInputAssemblyState = &InputAssembly;
  SubpassInfo.pViewportState = &ViewportState;
  SubpassInfo.pRasterizationState = &Raster;
  SubpassInfo.pMultisampleState = &Multisample;
  SubpassInfo.pColorBlendState = &Blend;
  SubpassInfo.layout = SubpassLayout;
  SubpassInfo.pNext = &Rendering;
  VkPipeline SubpassPipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &SubpassInfo,
                                      nullptr, &SubpassPipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);

  // `VK_ATTACHMENT_LOAD_OP_LOAD`: preserve the per-sample seed above rather
  // than clearing it away (a clear would write the same value to every
  // sample, defeating the whole point of this test).
  VkRenderingAttachmentInfo ColorAttachment{};
  ColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  ColorAttachment.imageView = MSView;
  ColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

  VkRenderingInfo RenderingInfo{};
  RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  RenderingInfo.renderArea = {{0, 0}, {Extent, Extent}};
  RenderingInfo.layerCount = 1;
  RenderingInfo.colorAttachmentCount = 1;
  RenderingInfo.pColorAttachments = &ColorAttachment;

  vkCmdBeginRenderingKHR(Cmd, &RenderingInfo);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, SubpassPipe);
  vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, SubpassLayout,
                          0, 1, &Set, 0, nullptr);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderingKHR(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  const auto *Data =
      static_cast<const uint8_t *>(fromHandle<Image>(MSImage)->data());
  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      // Every sample of this pixel now holds the same, uniformly-written
      // output (see this test's own comment), so sample 0 is as good a
      // read-back as any other.
      size_t Off = ((size_t)(Y * Extent + X) * SampleCount + 0) * 4;
      EXPECT_EQ(Data[Off + 0], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Data[Off + 1], 128) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Data[Off + 2], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Data[Off + 3], 0xFF) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, SubpassPipe, nullptr);
  vkDestroyShaderModule(Device, SubpassFragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyDescriptorPool(Device, DescPool, nullptr);
  vkDestroyPipelineLayout(Device, SubpassLayout, nullptr);
  vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
  vkDestroyImageView(Device, MSView, nullptr);
  vkDestroyImage(Device, MSImage, nullptr);
  vkFreeMemory(Device, MSMemory, nullptr);
}

/// Roadmap H7p: a classic (non-dynamic-rendering) `VkRenderPass` with two
/// subpasses -- subpass 0's own color attachment is a genuinely
/// multisampled (`VK_SAMPLE_COUNT_4_BIT`) attachment 0 (its per-sample
/// data seeded directly, like `SubpassLoadReadsBackAnExplicitSampleOfThe
/// ColorAttachmentItWrote`'s own comment explains, since a uniform draw
/// can never produce distinct per-sample values to read back distinctly);
/// subpass 1's own pipeline is a *different*, single-sample
/// (`VK_SAMPLE_COUNT_1_BIT`) one, writing a single-sample attachment 1,
/// that reads attachment 0 back via `subpassLoad`'s explicit-sample form
/// (`SubpassLoadMultisampleFragmentSource`, sample 2) -- exactly the real
/// `dEQP-VK.pipeline.monolithic.multisample.min_sample_shading_enabled.*`
/// shape (a later, single-sample `copy_sample_frag` subpass reading back
/// an earlier, multisampled subpass's own color output) that exposed a
/// real bug in `buildSubpassInputHeap` (CommandBuffer.cpp): it used the
/// *current, single-sample* pipeline's own `SampleCount` (`1`) for the
/// input attachment's `RowPitch`/`SampleStride` too, rather than that
/// input attachment's own real sample count (`4`) -- `RowPitch` came out
/// four times too small and `SampleStride` an outright `0` (every
/// `subpassLoad` sample aliasing to the same, wrong texel), silently
/// misaddressing every read instead of failing outright, which is what
/// actually produced the `min_sample_shading_enabled`/`sample_id` CTS
/// failures this roadmap entry originally, incompletely, attributed to
/// `gl_FragCoord` alone. Before the fix, this test's own read-back below
/// would not reliably show sample 2's own seeded red value (128); after
/// it, it does.
TEST_F(DrawTest,
       SubpassLoadReadsAnEarlierSubpassMultisampledColorOutputWithADifferentPipelineSampleCount) {
  constexpr uint32_t MSSampleCount = 4;

  VkImage MSImage = VK_NULL_HANDLE, OutputImage = VK_NULL_HANDLE;
  VkDeviceMemory MSMemory = VK_NULL_HANDLE, OutputMemory = VK_NULL_HANDLE;
  VkImageView MSView = VK_NULL_HANDLE, OutputView = VK_NULL_HANDLE;
  createImageAndView(VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT, MSImage, MSView, MSMemory,
                     VK_SAMPLE_COUNT_4_BIT);
  createImageAndView(VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT, OutputImage, OutputView,
                     OutputMemory, VK_SAMPLE_COUNT_1_BIT);

  // Every texel's 4 samples are seeded (0, 0, 0, 255), (64, 0, 0, 255),
  // (128, 0, 0, 255), (192, 0, 0, 255) -- sample 2's red channel is
  // therefore 128 everywhere, matching
  // `SubpassLoadReadsBackAnExplicitSampleOfTheColorAttachmentItWrote`'s
  // own seed.
  std::vector<uint8_t> Seed(Extent * Extent * MSSampleCount * 4);
  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X)
      for (uint32_t S = 0; S != MSSampleCount; ++S) {
        size_t Off = ((size_t)(Y * Extent + X) * MSSampleCount + S) * 4;
        Seed[Off + 0] = static_cast<uint8_t>(S * 64);
        Seed[Off + 1] = 0;
        Seed[Off + 2] = 0;
        Seed[Off + 3] = 0xFF;
      }
  std::memcpy(fromHandle<Image>(MSImage)->data(), Seed.data(), Seed.size());

  VkAttachmentDescription Attachments[2]{};
  Attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachments[0].samples = VK_SAMPLE_COUNT_4_BIT;
  Attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  Attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  Attachments[1].format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;

  VkAttachmentReference ColorRef0{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference ColorRef1{1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference InputRef{0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

  VkSubpassDescription Subpasses[2]{};
  Subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpasses[0].colorAttachmentCount = 1;
  Subpasses[0].pColorAttachments = &ColorRef0;
  Subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpasses[1].inputAttachmentCount = 1;
  Subpasses[1].pInputAttachments = &InputRef;
  Subpasses[1].colorAttachmentCount = 1;
  Subpasses[1].pColorAttachments = &ColorRef1;

  VkRenderPassCreateInfo PassInfo{};
  PassInfo.attachmentCount = 2;
  PassInfo.pAttachments = Attachments;
  PassInfo.subpassCount = 2;
  PassInfo.pSubpasses = Subpasses;
  VkRenderPass Pass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &Pass), VK_SUCCESS);

  VkImageView FbAttachments[2] = {MSView, OutputView};
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = Pass;
  FbInfo.attachmentCount = 2;
  FbInfo.pAttachments = FbAttachments;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = 1;
  VkFramebuffer Fb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &Fb), VK_SUCCESS);

  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule SubpassFragment =
      createModule(SubpassLoadMultisampleFragmentSource);

  VkDescriptorSetLayoutBinding Binding{};
  Binding.binding = 0;
  Binding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  Binding.descriptorCount = 1;
  Binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
  SetLayoutInfo.bindingCount = 1;
  SetLayoutInfo.pBindings = &Binding;
  VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreateDescriptorSetLayout(Device, &SetLayoutInfo, nullptr, &SetLayout),
      VK_SUCCESS);
  VkPipelineLayoutCreateInfo SubpassLayoutInfo{};
  SubpassLayoutInfo.setLayoutCount = 1;
  SubpassLayoutInfo.pSetLayouts = &SetLayout;
  VkPipelineLayout SubpassLayout = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineLayout(Device, &SubpassLayoutInfo, nullptr,
                                   &SubpassLayout),
            VK_SUCCESS);

  VkDescriptorPoolSize PoolSize{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1};
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
  VkDescriptorImageInfo ImageInfo{};
  ImageInfo.imageView = MSView;
  ImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  VkWriteDescriptorSet Write{};
  Write.dstSet = Set;
  Write.dstBinding = 0;
  Write.descriptorCount = 1;
  Write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  Write.pImageInfo = &ImageInfo;
  vkUpdateDescriptorSets(Device, 1, &Write, 0, nullptr);

  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;

  // Subpass 0's own pipeline is never actually drawn with (attachment 0's
  // data is seeded directly above), but a subpass still needs no bound
  // pipeline to simply be skipped over via `vkCmdNextSubpass` -- no
  // pipeline is created for it at all.

  // Subpass 1's own pipeline: a genuinely *different* sample count
  // (`VK_SAMPLE_COUNT_1_BIT`) than attachment 0's own `VK_SAMPLE_COUNT_
  // 4_BIT` -- the mismatch this test exists to exercise.
  VkPipelineMultisampleStateCreateInfo SubpassMultisample{};
  SubpassMultisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineShaderStageCreateInfo SubpassStages[2]{};
  SubpassStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  SubpassStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  SubpassStages[0].module = Vertex;
  SubpassStages[0].pName = "main";
  SubpassStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  SubpassStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  SubpassStages[1].module = SubpassFragment;
  SubpassStages[1].pName = "main";
  VkGraphicsPipelineCreateInfo SubpassInfo{};
  SubpassInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  SubpassInfo.stageCount = 2;
  SubpassInfo.pStages = SubpassStages;
  SubpassInfo.pVertexInputState = &VertexInput;
  SubpassInfo.pInputAssemblyState = &InputAssembly;
  SubpassInfo.pViewportState = &ViewportState;
  SubpassInfo.pRasterizationState = &Raster;
  SubpassInfo.pMultisampleState = &SubpassMultisample;
  SubpassInfo.pColorBlendState = &Blend;
  SubpassInfo.layout = SubpassLayout;
  SubpassInfo.renderPass = Pass;
  SubpassInfo.subpass = 1;
  VkPipeline SubpassPipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &SubpassInfo,
                                      nullptr, &SubpassPipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValues[2]{};
  ClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  ClearValues[1].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = Pass;
  PassBegin.framebuffer = Fb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 2;
  PassBegin.pClearValues = ClearValues;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  // Subpass 0: nothing to draw -- attachment 0's per-sample data is
  // already seeded above and `VK_ATTACHMENT_LOAD_OP_LOAD` preserves it.
  vkCmdNextSubpass(Cmd, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, SubpassPipe);
  vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, SubpassLayout,
                          0, 1, &Set, 0, nullptr);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // Attachment 1's own single sample must show sample 2's own seeded red
  // value (128) in green, everywhere -- not the wrong-sample/misaligned
  // garbage `buildSubpassInputHeap`'s pre-fix `RowPitch`/`SampleStride`
  // (computed from subpass 1's own single-sample pipeline instead of
  // attachment 0's real 4-sample count) actually produced.
  const auto *Data =
      static_cast<const uint8_t *>(fromHandle<Image>(OutputImage)->data());
  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      size_t Off = ((size_t)Y * Extent + X) * 4;
      EXPECT_EQ(Data[Off + 0], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Data[Off + 1], 128) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Data[Off + 2], 0x00) << "at (" << X << ", " << Y << ")";
      EXPECT_EQ(Data[Off + 3], 0xFF) << "at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, SubpassPipe, nullptr);
  vkDestroyShaderModule(Device, SubpassFragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyDescriptorPool(Device, DescPool, nullptr);
  vkDestroyPipelineLayout(Device, SubpassLayout, nullptr);
  vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
  vkDestroyFramebuffer(Device, Fb, nullptr);
  vkDestroyRenderPass(Device, Pass, nullptr);
  vkDestroyImageView(Device, OutputView, nullptr);
  vkDestroyImage(Device, OutputImage, nullptr);
  vkFreeMemory(Device, OutputMemory, nullptr);
  vkDestroyImageView(Device, MSView, nullptr);
  vkDestroyImage(Device, MSImage, nullptr);
  vkFreeMemory(Device, MSMemory, nullptr);
}


/// ViewIndex`) writes a different color per multiview view, into a
/// two-layer framebuffer bound by a two-view (`viewMask == 0b11`) render
/// pass -- confirming `CommandBuffer.cpp`'s `runDraw` both runs the
/// pipeline once per view and routes each view's draw into its own
/// same-numbered attachment array layer, with no explicit `gl_Layer`
/// write needed.
constexpr llvm::StringLiteral ViewIndexFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, MultiView], [SPV_KHR_multiview]> {
  spirv.GlobalVariable @view_index built_in("ViewIndex") : !spirv.ptr<i32, Input>
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %vip = spirv.mlir.addressof @view_index : !spirv.ptr<i32, Input>
    %vi = spirv.Load "Input" %vip : i32
    %c0 = spirv.Constant 0 : i32
    %is0 = spirv.IEqual %vi, %c0 : i32
    %one = spirv.Constant 1.0 : f32
    %zero = spirv.Constant 0.0 : f32
    %r = spirv.Select %is0, %one, %zero : i1, f32
    %g = spirv.Select %is0, %zero, %one : i1, f32
    %col = spirv.CompositeConstruct %r, %g, %zero, %one : (f32, f32, f32, f32) -> vector<4xf32>
    %colp = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %colp, %col : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @view_index, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir";

TEST_F(DrawTest, MultiviewRendersDifferentColorPerViewIntoItsOwnLayer) {
  constexpr uint32_t LayerCount = 2;

  VkImage LayeredImage = VK_NULL_HANDLE;
  VkDeviceMemory LayeredMemory = VK_NULL_HANDLE;
  VkImageCreateInfo ImageInfo{};
  ImageInfo.imageType = VK_IMAGE_TYPE_2D;
  ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ImageInfo.extent = {Extent, Extent, 1};
  ImageInfo.mipLevels = 1;
  ImageInfo.arrayLayers = LayerCount;
  ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  ImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &LayeredImage),
            VK_SUCCESS);
  VkMemoryRequirements Reqs{};
  vkGetImageMemoryRequirements(Device, LayeredImage, &Reqs);
  VkMemoryAllocateInfo MemAllocInfo{};
  MemAllocInfo.allocationSize = Reqs.size;
  ASSERT_EQ(vkAllocateMemory(Device, &MemAllocInfo, nullptr, &LayeredMemory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindImageMemory(Device, LayeredImage, LayeredMemory, 0),
            VK_SUCCESS);

  VkImageView LayeredView = VK_NULL_HANDLE;
  VkImageViewCreateInfo ViewInfo{};
  ViewInfo.image = LayeredImage;
  ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  ViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  ViewInfo.subresourceRange.levelCount = 1;
  ViewInfo.subresourceRange.layerCount = LayerCount;
  ASSERT_EQ(vkCreateImageView(Device, &ViewInfo, nullptr, &LayeredView),
            VK_SUCCESS);

  VkAttachmentDescription Attachment{};
  Attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  Attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 1;
  Subpass.pColorAttachments = &ColorRef;

  uint32_t ViewMask = 0b11;
  VkRenderPassMultiviewCreateInfo MultiviewInfo{};
  MultiviewInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
  MultiviewInfo.subpassCount = 1;
  MultiviewInfo.pViewMasks = &ViewMask;

  VkRenderPassCreateInfo PassInfo{};
  PassInfo.pNext = &MultiviewInfo;
  PassInfo.attachmentCount = 1;
  PassInfo.pAttachments = &Attachment;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass MultiviewPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &MultiviewPass),
            VK_SUCCESS);

  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = MultiviewPass;
  FbInfo.attachmentCount = 1;
  FbInfo.pAttachments = &LayeredView;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = LayerCount;
  VkFramebuffer MultiviewFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &MultiviewFb),
            VK_SUCCESS);

  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(ViewIndexFragmentSource);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;

  VkGraphicsPipelineCreateInfo PipeInfo{};
  PipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  PipeInfo.stageCount = 2;
  PipeInfo.pStages = Stages;
  PipeInfo.pVertexInputState = &VertexInput;
  PipeInfo.pInputAssemblyState = &InputAssembly;
  PipeInfo.pViewportState = &ViewportState;
  PipeInfo.pRasterizationState = &Raster;
  PipeInfo.pMultisampleState = &Multisample;
  PipeInfo.pColorBlendState = &Blend;
  PipeInfo.layout = Layout;
  PipeInfo.renderPass = MultiviewPass;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &PipeInfo,
                                      nullptr, &Pipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValue{};
  ClearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = MultiviewPass;
  PassBegin.framebuffer = MultiviewFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 1;
  PassBegin.pClearValues = &ClearValue;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // View 0 writes layer 0 red, view 1 writes layer 1 green -- each
  // view's own layer, not both views clobbering layer 0.
  const auto *Data =
      static_cast<const uint8_t *>(fromHandle<Image>(LayeredImage)->data());
  size_t LayerSizeBytes = (size_t)Extent * Extent * 4;
  const uint8_t *Layer0 = Data;
  const uint8_t *Layer1 = Data + LayerSizeBytes;
  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      size_t Off = ((size_t)Y * Extent + X) * 4;
      EXPECT_EQ(Layer0[Off + 0], 0xFF)
          << "layer 0 at (" << X << ", " << Y << ")";
      EXPECT_EQ(Layer0[Off + 1], 0x00)
          << "layer 0 at (" << X << ", " << Y << ")";
      EXPECT_EQ(Layer1[Off + 0], 0x00)
          << "layer 1 at (" << X << ", " << Y << ")";
      EXPECT_EQ(Layer1[Off + 1], 0xFF)
          << "layer 1 at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyFramebuffer(Device, MultiviewFb, nullptr);
  vkDestroyRenderPass(Device, MultiviewPass, nullptr);
  vkDestroyImageView(Device, LayeredView, nullptr);
  vkDestroyImage(Device, LayeredImage, nullptr);
  vkFreeMemory(Device, LayeredMemory, nullptr);
}

/// (Roadmap H5e-e) A vertex stage with an entirely empty interface --
/// mirrors `EmptyGeometrySource`/tessellation's own `void main(void) {}`
/// shape above -- legal here because the paired geometry stage
/// (`LayerOneLayeredGeometrySource`, below) neither reads any of
/// `gl_in[]`'s per-vertex outputs nor needs a per-vertex `Position` input:
/// it emits its own three hardcoded, oversized-triangle vertices outright.
constexpr llvm::StringLiteral EmptyVertexSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @main() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @main
}
)mlir";

/// (Roadmap H5e-e) A real, `EmitVertex`/`EndPrimitive`-driven geometry
/// stage -- unlike `GeometrySource`/`EmptyGeometrySource`
/// (`GraphicsPipelineTest.cpp`), which only exercise pipeline-creation
/// acceptance and never actually run -- that writes a constant
/// `gl_Layer = 1` once, then emits one oversized CCW triangle covering
/// the whole viewport into that layer via three `spirv.EmitVertex`s and a
/// closing `spirv.EndPrimitive`. Pairs with a plain (non-multiview,
/// `viewMask == 0`) two-layer render pass: this is the regression shape
/// for `dEQP-VK.geometry.layered.*.render_to_one`/
/// `multiple_layers_per_invocation`, where a geometry stage's own
/// `gl_Layer` output -- not multiview's `gl_ViewIndex` -- is what must
/// route the primitive to a non-zero attachment array layer.
constexpr llvm::StringLiteral LayerOneLayeredGeometrySource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Geometry], []> {
  spirv.GlobalVariable @out_pos built_in("Position") : !spirv.ptr<vector<4xf32>, Output>
  spirv.GlobalVariable @out_layer built_in("Layer") : !spirv.ptr<i32, Output>
  spirv.func @main() -> () "None" {
    %layer = spirv.Constant 1 : i32
    %layerp = spirv.mlir.addressof @out_layer : !spirv.ptr<i32, Output>
    spirv.Store "Output" %layerp, %layer : i32
    %posp = spirv.mlir.addressof @out_pos : !spirv.ptr<vector<4xf32>, Output>
    %neg1 = spirv.Constant -1.0 : f32
    %three = spirv.Constant 3.0 : f32
    %z = spirv.Constant 0.0 : f32
    %w = spirv.Constant 1.0 : f32

    %p0 = spirv.CompositeConstruct %neg1, %neg1, %z, %w : (f32, f32, f32, f32) -> vector<4xf32>
    spirv.Store "Output" %posp, %p0 : vector<4xf32>
    spirv.EmitVertex

    %p1 = spirv.CompositeConstruct %three, %neg1, %z, %w : (f32, f32, f32, f32) -> vector<4xf32>
    spirv.Store "Output" %posp, %p1 : vector<4xf32>
    spirv.EmitVertex

    %p2 = spirv.CompositeConstruct %neg1, %three, %z, %w : (f32, f32, f32, f32) -> vector<4xf32>
    spirv.Store "Output" %posp, %p2 : vector<4xf32>
    spirv.EmitVertex
    spirv.EndPrimitive
    spirv.Return
  }
  spirv.EntryPoint "Geometry" @main, @out_pos, @out_layer
  spirv.ExecutionMode @main "Triangles"
  spirv.ExecutionMode @main "OutputTriangleStrip"
  spirv.ExecutionMode @main "OutputVertices", 3
}
)mlir";

/// (Roadmap H5e-e) A geometry stage's own `gl_Layer` output routes a draw
/// into a non-zero attachment array layer of a plain (non-multiview)
/// layered render target -- exactly the shape
/// `dEQP-VK.geometry.layered.*.render_to_one` exercises and that rendered
/// nothing at all into any layer before this fix: `CommandBuffer.cpp`'s
/// `runDraw` used to unconditionally slice every attachment down to a
/// single array layer (layer 0) once per "view" even when there was no
/// real multiview (`viewMask == 0`, one always-`ViewIndex == 0` loop
/// iteration) -- destroying every layer but 0 before `Executor.cpp`'s own
/// per-primitive `gl_Layer` routing (`resolveRenderTargetArrayLayer`) ever
/// got a chance to see them, so a primitive routed to any layer but 0 was
/// silently discarded as out of range. This test's target layer (1) is
/// also left an untouched `LOAD_OP_CLEAR` layer in every other case in
/// this file, so it doubles as a regression test for the *other* half of
/// this fix: clearing a plain layered attachment must clear every one of
/// its layers up front (`fullLayerMask`), not just layer 0 -- layer 0
/// here must read back as cleared black, not left as whatever
/// uninitialized bytes its backing memory happened to start with.
TEST_F(DrawTest, GeometryStageLayerOutputRoutesToANonMultiviewLayer) {
  constexpr uint32_t LayerCount = 2;

  VkImage LayeredImage = VK_NULL_HANDLE;
  VkDeviceMemory LayeredMemory = VK_NULL_HANDLE;
  VkImageCreateInfo ImageInfo{};
  ImageInfo.imageType = VK_IMAGE_TYPE_2D;
  ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ImageInfo.extent = {Extent, Extent, 1};
  ImageInfo.mipLevels = 1;
  ImageInfo.arrayLayers = LayerCount;
  ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  ImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &LayeredImage),
            VK_SUCCESS);
  VkMemoryRequirements Reqs{};
  vkGetImageMemoryRequirements(Device, LayeredImage, &Reqs);
  VkMemoryAllocateInfo MemAllocInfo{};
  MemAllocInfo.allocationSize = Reqs.size;
  ASSERT_EQ(vkAllocateMemory(Device, &MemAllocInfo, nullptr, &LayeredMemory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindImageMemory(Device, LayeredImage, LayeredMemory, 0),
            VK_SUCCESS);

  VkImageView LayeredView = VK_NULL_HANDLE;
  VkImageViewCreateInfo ViewInfo{};
  ViewInfo.image = LayeredImage;
  ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  ViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  ViewInfo.subresourceRange.levelCount = 1;
  ViewInfo.subresourceRange.layerCount = LayerCount;
  ASSERT_EQ(vkCreateImageView(Device, &ViewInfo, nullptr, &LayeredView),
            VK_SUCCESS);

  // A plain (no `VkRenderPassMultiviewCreateInfo`) single-subpass render
  // pass: `viewMask == 0` throughout.
  VkAttachmentDescription Attachment{};
  Attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  Attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 1;
  Subpass.pColorAttachments = &ColorRef;
  VkRenderPassCreateInfo PassInfo{};
  PassInfo.attachmentCount = 1;
  PassInfo.pAttachments = &Attachment;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass LayeredPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &LayeredPass),
            VK_SUCCESS);

  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = LayeredPass;
  FbInfo.attachmentCount = 1;
  FbInfo.pAttachments = &LayeredView;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = LayerCount;
  VkFramebuffer LayeredFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &LayeredFb),
            VK_SUCCESS);

  VkShaderModule VertexModule = createModule(EmptyVertexSource);
  VkShaderModule GeometryModule = createModule(LayerOneLayeredGeometrySource);
  VkShaderModule FragmentModule = createModule(RedFragmentSource);

  VkPipelineShaderStageCreateInfo Stages[3]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = VertexModule;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
  Stages[1].module = GeometryModule;
  Stages[1].pName = "main";
  Stages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[2].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[2].module = FragmentModule;
  Stages[2].pName = "main";

  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;

  VkGraphicsPipelineCreateInfo PipeInfo{};
  PipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  PipeInfo.stageCount = 3;
  PipeInfo.pStages = Stages;
  PipeInfo.pVertexInputState = &VertexInput;
  PipeInfo.pInputAssemblyState = &InputAssembly;
  PipeInfo.pViewportState = &ViewportState;
  PipeInfo.pRasterizationState = &Raster;
  PipeInfo.pMultisampleState = &Multisample;
  PipeInfo.pColorBlendState = &Blend;
  PipeInfo.layout = Layout;
  PipeInfo.renderPass = LayeredPass;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &PipeInfo,
                                      nullptr, &Pipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValue{};
  ClearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = LayeredPass;
  PassBegin.framebuffer = LayeredFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 1;
  PassBegin.pClearValues = &ClearValue;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // Layer 0 was never the geometry stage's `gl_Layer` target: it must
  // read back as `LOAD_OP_CLEAR`'s own solid black, not garbage. Layer 1
  // is the geometry stage's actual `gl_Layer == 1` target: it must read
  // back as the fragment stage's solid red.
  const auto *Data =
      static_cast<const uint8_t *>(fromHandle<Image>(LayeredImage)->data());
  size_t LayerSizeBytes = (size_t)Extent * Extent * 4;
  const uint8_t *Layer0 = Data;
  const uint8_t *Layer1 = Data + LayerSizeBytes;
  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      size_t Off = ((size_t)Y * Extent + X) * 4;
      EXPECT_EQ(Layer0[Off + 0], 0x00)
          << "layer 0 (untouched) at (" << X << ", " << Y << ")";
      EXPECT_EQ(Layer0[Off + 1], 0x00)
          << "layer 0 (untouched) at (" << X << ", " << Y << ")";
      EXPECT_EQ(Layer0[Off + 2], 0x00)
          << "layer 0 (untouched) at (" << X << ", " << Y << ")";
      EXPECT_EQ(Layer0[Off + 3], 0xFF)
          << "layer 0 (untouched) at (" << X << ", " << Y << ")";
      EXPECT_EQ(Layer1[Off + 0], 0xFF)
          << "layer 1 (gl_Layer target) at (" << X << ", " << Y << ")";
      EXPECT_EQ(Layer1[Off + 1], 0x00)
          << "layer 1 (gl_Layer target) at (" << X << ", " << Y << ")";
      EXPECT_EQ(Layer1[Off + 2], 0x00)
          << "layer 1 (gl_Layer target) at (" << X << ", " << Y << ")";
      EXPECT_EQ(Layer1[Off + 3], 0xFF)
          << "layer 1 (gl_Layer target) at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, FragmentModule, nullptr);
  vkDestroyShaderModule(Device, GeometryModule, nullptr);
  vkDestroyShaderModule(Device, VertexModule, nullptr);
  vkDestroyFramebuffer(Device, LayeredFb, nullptr);
  vkDestroyRenderPass(Device, LayeredPass, nullptr);
  vkDestroyImageView(Device, LayeredView, nullptr);
  vkDestroyImage(Device, LayeredImage, nullptr);
  vkFreeMemory(Device, LayeredMemory, nullptr);
}

/// (Roadmap H2h) A classic `VkRenderPass`'s later subpass reading an
/// *earlier* subpass's own color output back through a `subpassInput`,
/// under multiview -- exactly the shape `dEQP-VK.multiview.
/// input_attachments` exercises and that rendered a totally blank image in
/// every one of its 16 cases before this fix: subpass 0 writes attachment
/// 0 solid red (once per view, into that view's own array layer); subpass
/// 1 declares attachment 0 as its own input attachment (not one of its own
/// color attachments, which is attachment 1 instead) and reads it back
/// through `SubpassLoadFragmentSource`, writing solid green to attachment
/// 1. `RenderTargetBinding::Inputs` (`buildRenderTargetBinding`) is what
/// makes attachment 0 visible to subpass 1's `buildSubpassInputHeap` call
/// at all: before this fix, `Attachments` (subpass 1's own color
/// attachments, i.e. just attachment 1) never contained attachment 0, so
/// `ColorIndexFor`'s identity-mapping fallback address `subpassLoad`'s
/// `attachment_index == 0` against attachment 1's own (freshly-cleared)
/// data instead, reading transparent black -- rendering every pixel of
/// every view blank rather than green.
TEST_F(DrawTest, MultiviewInputAttachmentReadsBackAnEarlierSubpassColorOutput) {
  constexpr uint32_t LayerCount = 2;

  auto createLayeredColorAttachment = [&](VkImage &Image,
                                          VkDeviceMemory &Memory,
                                          VkImageView &View) {
    VkImageCreateInfo ImageInfo{};
    ImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    ImageInfo.extent = {Extent, Extent, 1};
    ImageInfo.mipLevels = 1;
    ImageInfo.arrayLayers = LayerCount;
    ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &Image), VK_SUCCESS);
    VkMemoryRequirements Reqs{};
    vkGetImageMemoryRequirements(Device, Image, &Reqs);
    VkMemoryAllocateInfo MemAllocInfo{};
    MemAllocInfo.allocationSize = Reqs.size;
    ASSERT_EQ(vkAllocateMemory(Device, &MemAllocInfo, nullptr, &Memory),
              VK_SUCCESS);
    ASSERT_EQ(vkBindImageMemory(Device, Image, Memory, 0), VK_SUCCESS);

    VkImageViewCreateInfo ViewInfo{};
    ViewInfo.image = Image;
    ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    ViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewInfo.subresourceRange.levelCount = 1;
    ViewInfo.subresourceRange.layerCount = LayerCount;
    ASSERT_EQ(vkCreateImageView(Device, &ViewInfo, nullptr, &View), VK_SUCCESS);
  };

  VkImage InputImage = VK_NULL_HANDLE, OutputImage = VK_NULL_HANDLE;
  VkDeviceMemory InputMemory = VK_NULL_HANDLE, OutputMemory = VK_NULL_HANDLE;
  VkImageView InputLayeredView = VK_NULL_HANDLE,
              OutputLayeredView = VK_NULL_HANDLE;
  createLayeredColorAttachment(InputImage, InputMemory, InputLayeredView);
  createLayeredColorAttachment(OutputImage, OutputMemory, OutputLayeredView);

  VkAttachmentDescription Attachments[2]{};
  Attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  Attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  Attachments[1] = Attachments[0];

  VkAttachmentReference ColorRef0{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference ColorRef1{1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference InputRef{0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

  VkSubpassDescription Subpasses[2]{};
  Subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpasses[0].colorAttachmentCount = 1;
  Subpasses[0].pColorAttachments = &ColorRef0;
  Subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpasses[1].inputAttachmentCount = 1;
  Subpasses[1].pInputAttachments = &InputRef;
  Subpasses[1].colorAttachmentCount = 1;
  Subpasses[1].pColorAttachments = &ColorRef1;

  uint32_t ViewMasks[2] = {0b11, 0b11};
  VkRenderPassMultiviewCreateInfo MultiviewInfo{};
  MultiviewInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
  MultiviewInfo.subpassCount = 2;
  MultiviewInfo.pViewMasks = ViewMasks;

  VkRenderPassCreateInfo PassInfo{};
  PassInfo.pNext = &MultiviewInfo;
  PassInfo.attachmentCount = 2;
  PassInfo.pAttachments = Attachments;
  PassInfo.subpassCount = 2;
  PassInfo.pSubpasses = Subpasses;
  VkRenderPass MultiviewPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &MultiviewPass),
            VK_SUCCESS);

  VkImageView FbAttachments[2] = {InputLayeredView, OutputLayeredView};
  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = MultiviewPass;
  FbInfo.attachmentCount = 2;
  FbInfo.pAttachments = FbAttachments;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = LayerCount;
  VkFramebuffer MultiviewFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &MultiviewFb),
            VK_SUCCESS);

  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule RedFragment = createModule(RedFragmentSource);
  VkShaderModule SubpassFragment = createModule(SubpassLoadFragmentSource);

  // `SubpassLoadFragmentSource`'s own `bind(0, 0)` needs a matching
  // descriptor set layout entry to compile against, even though roadmap
  // F8a's read never consults whatever gets bound there at draw time (see
  // `SubpassLoadReadsBackTheColorAttachmentItWrote`'s own comment).
  VkDescriptorSetLayoutBinding Binding{};
  Binding.binding = 0;
  Binding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  Binding.descriptorCount = 1;
  Binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
  SetLayoutInfo.bindingCount = 1;
  SetLayoutInfo.pBindings = &Binding;
  VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreateDescriptorSetLayout(Device, &SetLayoutInfo, nullptr, &SetLayout),
      VK_SUCCESS);
  VkPipelineLayoutCreateInfo SubpassLayoutInfo{};
  SubpassLayoutInfo.setLayoutCount = 1;
  SubpassLayoutInfo.pSetLayouts = &SetLayout;
  VkPipelineLayout SubpassLayout = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineLayout(Device, &SubpassLayoutInfo, nullptr,
                                   &SubpassLayout),
            VK_SUCCESS);

  VkDescriptorPoolSize PoolSize{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1};
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
  VkDescriptorImageInfo ImageInfo{};
  ImageInfo.imageView = InputLayeredView;
  ImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  VkWriteDescriptorSet Write{};
  Write.dstSet = Set;
  Write.dstBinding = 0;
  Write.descriptorCount = 1;
  Write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  Write.pImageInfo = &ImageInfo;
  vkUpdateDescriptorSets(Device, 1, &Write, 0, nullptr);

  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;

  VkPipelineShaderStageCreateInfo RedStages[2]{};
  RedStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  RedStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  RedStages[0].module = Vertex;
  RedStages[0].pName = "main";
  RedStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  RedStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  RedStages[1].module = RedFragment;
  RedStages[1].pName = "main";
  VkGraphicsPipelineCreateInfo RedInfo{};
  RedInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  RedInfo.stageCount = 2;
  RedInfo.pStages = RedStages;
  RedInfo.pVertexInputState = &VertexInput;
  RedInfo.pInputAssemblyState = &InputAssembly;
  RedInfo.pViewportState = &ViewportState;
  RedInfo.pRasterizationState = &Raster;
  RedInfo.pMultisampleState = &Multisample;
  RedInfo.pColorBlendState = &Blend;
  RedInfo.layout = Layout;
  RedInfo.renderPass = MultiviewPass;
  RedInfo.subpass = 0;
  VkPipeline RedPipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &RedInfo,
                                      nullptr, &RedPipe),
            VK_SUCCESS);

  VkPipelineShaderStageCreateInfo SubpassStages[2]{};
  SubpassStages[0] = RedStages[0];
  SubpassStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  SubpassStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  SubpassStages[1].module = SubpassFragment;
  SubpassStages[1].pName = "main";
  VkGraphicsPipelineCreateInfo SubpassInfo{};
  SubpassInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  SubpassInfo.stageCount = 2;
  SubpassInfo.pStages = SubpassStages;
  SubpassInfo.pVertexInputState = &VertexInput;
  SubpassInfo.pInputAssemblyState = &InputAssembly;
  SubpassInfo.pViewportState = &ViewportState;
  SubpassInfo.pRasterizationState = &Raster;
  SubpassInfo.pMultisampleState = &Multisample;
  SubpassInfo.pColorBlendState = &Blend;
  SubpassInfo.layout = SubpassLayout;
  SubpassInfo.renderPass = MultiviewPass;
  SubpassInfo.subpass = 1;
  VkPipeline SubpassPipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &SubpassInfo,
                                      nullptr, &SubpassPipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValues[2]{};
  ClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  ClearValues[1].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = MultiviewPass;
  PassBegin.framebuffer = MultiviewFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 2;
  PassBegin.pClearValues = ClearValues;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, RedPipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdNextSubpass(Cmd, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, SubpassPipe);
  vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, SubpassLayout,
                          0, 1, &Set, 0, nullptr);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // Both views' own layer of the final (attachment 1) output must be
  // solid green, exactly like the non-multiview
  // `SubpassLoadReadsBackTheColorAttachmentItWrote` -- not blank, which is
  // what every one of `dEQP-VK.multiview.input_attachments`' 16 cases
  // rendered before this fix.
  const auto *Data =
      static_cast<const uint8_t *>(fromHandle<Image>(OutputImage)->data());
  size_t LayerSizeBytes = (size_t)Extent * Extent * 4;
  for (uint32_t Layer = 0; Layer != LayerCount; ++Layer) {
    const uint8_t *LayerData = Data + Layer * LayerSizeBytes;
    for (uint32_t Y = 0; Y != Extent; ++Y)
      for (uint32_t X = 0; X != Extent; ++X) {
        size_t Off = ((size_t)Y * Extent + X) * 4;
        EXPECT_EQ(LayerData[Off + 0], 0x00)
            << "layer " << Layer << " at (" << X << ", " << Y << ")";
        EXPECT_EQ(LayerData[Off + 1], 0xFF)
            << "layer " << Layer << " at (" << X << ", " << Y << ")";
        EXPECT_EQ(LayerData[Off + 2], 0x00)
            << "layer " << Layer << " at (" << X << ", " << Y << ")";
        EXPECT_EQ(LayerData[Off + 3], 0xFF)
            << "layer " << Layer << " at (" << X << ", " << Y << ")";
      }
  }

  vkDestroyPipeline(Device, RedPipe, nullptr);
  vkDestroyPipeline(Device, SubpassPipe, nullptr);
  vkDestroyShaderModule(Device, SubpassFragment, nullptr);
  vkDestroyShaderModule(Device, RedFragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyDescriptorPool(Device, DescPool, nullptr);
  vkDestroyPipelineLayout(Device, SubpassLayout, nullptr);
  vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
  vkDestroyFramebuffer(Device, MultiviewFb, nullptr);
  vkDestroyRenderPass(Device, MultiviewPass, nullptr);
  vkDestroyImageView(Device, InputLayeredView, nullptr);
  vkDestroyImageView(Device, OutputLayeredView, nullptr);
  vkDestroyImage(Device, InputImage, nullptr);
  vkDestroyImage(Device, OutputImage, nullptr);
  vkFreeMemory(Device, InputMemory, nullptr);
  vkFreeMemory(Device, OutputMemory, nullptr);
}

/// (Roadmap H2g) `vkCmdClearAttachments` inside a multiview render pass
/// instance clears every set `viewMask` bit's own attachment layer, not
/// just layer 0 -- the Vulkan spec's own multiview clear rule (a clear
/// rect's `baseArrayLayer`/`layerCount` are relative to the current
/// subpass's view mask, exactly like a draw's implicit per-view
/// replication -- see `sliceAttachmentLayer`'s own comment): a draw first
/// writes both layers red, then a `vkCmdClearAttachments` covering the
/// whole render area must turn both layers blue, not just layer 0 (this
/// row's own root cause, confirmed against a real `dEQP-VK.multiview.
/// clear_attachments` run: every layer past 0 kept its pre-clear content).
TEST_F(DrawTest, MultiviewClearAttachmentsClearsEveryViewsOwnLayer) {
  constexpr uint32_t LayerCount = 2;

  VkImage LayeredImage = VK_NULL_HANDLE;
  VkDeviceMemory LayeredMemory = VK_NULL_HANDLE;
  VkImageCreateInfo ImageInfo{};
  ImageInfo.imageType = VK_IMAGE_TYPE_2D;
  ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ImageInfo.extent = {Extent, Extent, 1};
  ImageInfo.mipLevels = 1;
  ImageInfo.arrayLayers = LayerCount;
  ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  ImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &LayeredImage),
            VK_SUCCESS);
  VkMemoryRequirements Reqs{};
  vkGetImageMemoryRequirements(Device, LayeredImage, &Reqs);
  VkMemoryAllocateInfo MemAllocInfo{};
  MemAllocInfo.allocationSize = Reqs.size;
  ASSERT_EQ(vkAllocateMemory(Device, &MemAllocInfo, nullptr, &LayeredMemory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindImageMemory(Device, LayeredImage, LayeredMemory, 0),
            VK_SUCCESS);

  VkImageView LayeredView = VK_NULL_HANDLE;
  VkImageViewCreateInfo ViewInfo{};
  ViewInfo.image = LayeredImage;
  ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  ViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  ViewInfo.subresourceRange.levelCount = 1;
  ViewInfo.subresourceRange.layerCount = LayerCount;
  ASSERT_EQ(vkCreateImageView(Device, &ViewInfo, nullptr, &LayeredView),
            VK_SUCCESS);

  VkAttachmentDescription Attachment{};
  Attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  Attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 1;
  Subpass.pColorAttachments = &ColorRef;

  uint32_t ViewMask = 0b11;
  VkRenderPassMultiviewCreateInfo MultiviewInfo{};
  MultiviewInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
  MultiviewInfo.subpassCount = 1;
  MultiviewInfo.pViewMasks = &ViewMask;

  VkRenderPassCreateInfo PassInfo{};
  PassInfo.pNext = &MultiviewInfo;
  PassInfo.attachmentCount = 1;
  PassInfo.pAttachments = &Attachment;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass MultiviewPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &MultiviewPass),
            VK_SUCCESS);

  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = MultiviewPass;
  FbInfo.attachmentCount = 1;
  FbInfo.pAttachments = &LayeredView;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = LayerCount;
  VkFramebuffer MultiviewFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &MultiviewFb),
            VK_SUCCESS);

  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;

  VkGraphicsPipelineCreateInfo PipeInfo{};
  PipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  PipeInfo.stageCount = 2;
  PipeInfo.pStages = Stages;
  PipeInfo.pVertexInputState = &VertexInput;
  PipeInfo.pInputAssemblyState = &InputAssembly;
  PipeInfo.pViewportState = &ViewportState;
  PipeInfo.pRasterizationState = &Raster;
  PipeInfo.pMultisampleState = &Multisample;
  PipeInfo.pColorBlendState = &Blend;
  PipeInfo.layout = Layout;
  PipeInfo.renderPass = MultiviewPass;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &PipeInfo,
                                      nullptr, &Pipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValue{};
  ClearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = MultiviewPass;
  PassBegin.framebuffer = MultiviewFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 1;
  PassBegin.pClearValues = &ClearValue;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);

  VkClearAttachment Clear{};
  Clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  Clear.colorAttachment = 0;
  Clear.clearValue.color = {{0.0f, 0.0f, 1.0f, 1.0f}};
  VkClearRect Rect{};
  Rect.rect = {{0, 0}, {Extent, Extent}};
  Rect.baseArrayLayer = 0;
  Rect.layerCount = 1;
  vkCmdClearAttachments(Cmd, 1, &Clear, 1, &Rect);

  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // Both views' own layers are blue -- the clear applied to every set
  // `viewMask` bit, not just layer 0.
  const auto *Data =
      static_cast<const uint8_t *>(fromHandle<Image>(LayeredImage)->data());
  size_t LayerSizeBytes = (size_t)Extent * Extent * 4;
  const uint8_t *Layer0 = Data;
  const uint8_t *Layer1 = Data + LayerSizeBytes;
  EXPECT_EQ(Layer0[2], 0xFF) << "layer 0 blue channel";
  EXPECT_EQ(Layer0[0], 0x00) << "layer 0 red channel";
  EXPECT_EQ(Layer1[2], 0xFF) << "layer 1 blue channel";
  EXPECT_EQ(Layer1[0], 0x00) << "layer 1 red channel";

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyFramebuffer(Device, MultiviewFb, nullptr);
  vkDestroyRenderPass(Device, MultiviewPass, nullptr);
  vkDestroyImageView(Device, LayeredView, nullptr);
  vkDestroyImage(Device, LayeredImage, nullptr);
  vkFreeMemory(Device, LayeredMemory, nullptr);
}

/// (Roadmap H2i) A classic, multi-subpass `VkRenderPass` where every
/// subpass shares one `VK_ATTACHMENT_LOAD_OP_CLEAR` color attachment but
/// each declares its own, disjoint-or-repeated multiview `viewMask` --
/// exactly `dEQP-VK.multiview.readback_implicit_clear`'s own multi-subpass
/// combinations (e.g. `1_2_4_8`, `5_10_5_10`), which all failed before this
/// fix while every single-subpass combination already passed. Three
/// subpasses: subpass 0 (`viewMask == 0b01`) draws solid red into view 0's
/// own layer; subpass 1 (`viewMask == 0b10`) draws nothing, relying solely
/// on the render pass's own load op to paint view 1's layer with the clear
/// color; subpass 2 (`viewMask == 0b01` again, re-using view 0) also draws
/// nothing. This exercises both of this row's own fixes at once:
/// (1) view 1 is a view `applyLoadOps` never saw at `vkCmdBeginRenderPass`
/// time (only view 0 was in subpass 0's own mask) -- before this fix,
/// `nextSubpass` never called `applyLoadOps` again at all, so view 1's own
/// layer was left with whatever the freshly bound image's own memory
/// already held, not the clear color; (2) subpass 2 re-uses view 0, which
/// subpass 0 already cleared *and* drew real content into -- naively
/// re-applying the clear at every `nextSubpass` (rather than gating it on
/// `GraphicsState::LoadedAttachmentViewMask`) would erase subpass 0's own
/// red draw.
TEST_F(DrawTest,
       MultiviewLoadOpClearAppliesEachSubpassOwnNewlyIntroducedViews) {
  constexpr uint32_t LayerCount = 2;

  VkImage LayeredImage = VK_NULL_HANDLE;
  VkDeviceMemory LayeredMemory = VK_NULL_HANDLE;
  VkImageCreateInfo ImageInfo{};
  ImageInfo.imageType = VK_IMAGE_TYPE_2D;
  ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ImageInfo.extent = {Extent, Extent, 1};
  ImageInfo.mipLevels = 1;
  ImageInfo.arrayLayers = LayerCount;
  ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  ImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &LayeredImage),
            VK_SUCCESS);
  VkMemoryRequirements Reqs{};
  vkGetImageMemoryRequirements(Device, LayeredImage, &Reqs);
  VkMemoryAllocateInfo MemAllocInfo{};
  MemAllocInfo.allocationSize = Reqs.size;
  ASSERT_EQ(vkAllocateMemory(Device, &MemAllocInfo, nullptr, &LayeredMemory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindImageMemory(Device, LayeredImage, LayeredMemory, 0),
            VK_SUCCESS);

  VkImageView LayeredView = VK_NULL_HANDLE;
  VkImageViewCreateInfo ViewInfo{};
  ViewInfo.image = LayeredImage;
  ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  ViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  ViewInfo.subresourceRange.levelCount = 1;
  ViewInfo.subresourceRange.layerCount = LayerCount;
  ASSERT_EQ(vkCreateImageView(Device, &ViewInfo, nullptr, &LayeredView),
            VK_SUCCESS);

  VkAttachmentDescription Attachment{};
  Attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  Attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription Subpasses[3]{};
  for (VkSubpassDescription &Subpass : Subpasses) {
    Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    Subpass.colorAttachmentCount = 1;
    Subpass.pColorAttachments = &ColorRef;
  }

  uint32_t ViewMasks[3] = {0b01, 0b10, 0b01};
  VkRenderPassMultiviewCreateInfo MultiviewInfo{};
  MultiviewInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
  MultiviewInfo.subpassCount = 3;
  MultiviewInfo.pViewMasks = ViewMasks;

  VkRenderPassCreateInfo PassInfo{};
  PassInfo.pNext = &MultiviewInfo;
  PassInfo.attachmentCount = 1;
  PassInfo.pAttachments = &Attachment;
  PassInfo.subpassCount = 3;
  PassInfo.pSubpasses = Subpasses;
  VkRenderPass MultiviewPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &MultiviewPass),
            VK_SUCCESS);

  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = MultiviewPass;
  FbInfo.attachmentCount = 1;
  FbInfo.pAttachments = &LayeredView;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = LayerCount;
  VkFramebuffer MultiviewFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &MultiviewFb),
            VK_SUCCESS);

  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;

  VkGraphicsPipelineCreateInfo PipeInfo{};
  PipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  PipeInfo.stageCount = 2;
  PipeInfo.pStages = Stages;
  PipeInfo.pVertexInputState = &VertexInput;
  PipeInfo.pInputAssemblyState = &InputAssembly;
  PipeInfo.pViewportState = &ViewportState;
  PipeInfo.pRasterizationState = &Raster;
  PipeInfo.pMultisampleState = &Multisample;
  PipeInfo.pColorBlendState = &Blend;
  PipeInfo.layout = Layout;
  PipeInfo.renderPass = MultiviewPass;
  PipeInfo.subpass = 0;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &PipeInfo,
                                      nullptr, &Pipe),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  VkClearValue ClearValue{};
  ClearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = MultiviewPass;
  PassBegin.framebuffer = MultiviewFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 1;
  PassBegin.pClearValues = &ClearValue;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  // Subpass 0 (view 0): draw solid red over the whole render area.
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  // Subpass 1 (view 1): no draw, relies only on the load op's own clear.
  vkCmdNextSubpass(Cmd, VK_SUBPASS_CONTENTS_INLINE);
  // Subpass 2 (view 0 again): no draw either -- must not re-clear over
  // subpass 0's own red.
  vkCmdNextSubpass(Cmd, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // Layer 0 (view 0) keeps subpass 0's own red draw, not re-cleared to
  // black by subpass 2 re-using the same view; layer 1 (view 1) is the
  // clear color, painted by its own share of the load op at subpass 1's
  // `vkCmdNextSubpass`, not left as whatever the fresh image's memory held.
  const auto *Data =
      static_cast<const uint8_t *>(fromHandle<Image>(LayeredImage)->data());
  size_t LayerSizeBytes = (size_t)Extent * Extent * 4;
  const uint8_t *Layer0 = Data;
  const uint8_t *Layer1 = Data + LayerSizeBytes;
  for (uint32_t Y = 0; Y != Extent; ++Y)
    for (uint32_t X = 0; X != Extent; ++X) {
      size_t Off = ((size_t)Y * Extent + X) * 4;
      EXPECT_EQ(Layer0[Off + 0], 0xFF)
          << "layer 0 red channel at (" << X << ", " << Y << ")";
      EXPECT_EQ(Layer0[Off + 1], 0x00)
          << "layer 0 green channel at (" << X << ", " << Y << ")";
      EXPECT_EQ(Layer1[Off + 0], 0x00)
          << "layer 1 red channel at (" << X << ", " << Y << ")";
      EXPECT_EQ(Layer1[Off + 3], 0xFF)
          << "layer 1 alpha channel at (" << X << ", " << Y << ")";
    }

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyFramebuffer(Device, MultiviewFb, nullptr);
  vkDestroyRenderPass(Device, MultiviewPass, nullptr);
  vkDestroyImageView(Device, LayeredView, nullptr);
  vkDestroyImage(Device, LayeredImage, nullptr);
  vkFreeMemory(Device, LayeredMemory, nullptr);
}

/// (Roadmap H2f) Occlusion queries recorded inside a multiview render pass
/// instance implicitly span one query index per set view-mask bit (the
/// Vulkan spec's own multiview query rule -- see `QueryPool.h`'s file
/// comment): a two-view (`viewMask == 0b11`) instance's single
/// `vkCmdBeginQuery`/`vkCmdEndQuery` pair at query index 0 must write both
/// query index 0 (view 0) *and* query index 1 (view 1), each with that
/// view's own passed-sample count and each independently available --
/// not just index 0, leaving index 1 permanently unavailable (this row's
/// own root cause, `dEQP-VK.multiview.non_precise_queries_with_
/// availability`'s "occlusion availability bit N is 0").
TEST_F(DrawTest, MultiviewOcclusionQueryWritesOneQueryIndexPerView) {
  constexpr uint32_t LayerCount = 2;

  VkImage LayeredImage = VK_NULL_HANDLE;
  VkDeviceMemory LayeredMemory = VK_NULL_HANDLE;
  VkImageCreateInfo ImageInfo{};
  ImageInfo.imageType = VK_IMAGE_TYPE_2D;
  ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ImageInfo.extent = {Extent, Extent, 1};
  ImageInfo.mipLevels = 1;
  ImageInfo.arrayLayers = LayerCount;
  ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  ImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  ASSERT_EQ(vkCreateImage(Device, &ImageInfo, nullptr, &LayeredImage),
            VK_SUCCESS);
  VkMemoryRequirements Reqs{};
  vkGetImageMemoryRequirements(Device, LayeredImage, &Reqs);
  VkMemoryAllocateInfo MemAllocInfo{};
  MemAllocInfo.allocationSize = Reqs.size;
  ASSERT_EQ(vkAllocateMemory(Device, &MemAllocInfo, nullptr, &LayeredMemory),
            VK_SUCCESS);
  ASSERT_EQ(vkBindImageMemory(Device, LayeredImage, LayeredMemory, 0),
            VK_SUCCESS);

  VkImageView LayeredView = VK_NULL_HANDLE;
  VkImageViewCreateInfo ViewInfo{};
  ViewInfo.image = LayeredImage;
  ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  ViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  ViewInfo.subresourceRange.levelCount = 1;
  ViewInfo.subresourceRange.layerCount = LayerCount;
  ASSERT_EQ(vkCreateImageView(Device, &ViewInfo, nullptr, &LayeredView),
            VK_SUCCESS);

  VkAttachmentDescription Attachment{};
  Attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  Attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentReference ColorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription Subpass{};
  Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  Subpass.colorAttachmentCount = 1;
  Subpass.pColorAttachments = &ColorRef;

  uint32_t ViewMask = 0b11;
  VkRenderPassMultiviewCreateInfo MultiviewInfo{};
  MultiviewInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
  MultiviewInfo.subpassCount = 1;
  MultiviewInfo.pViewMasks = &ViewMask;

  VkRenderPassCreateInfo PassInfo{};
  PassInfo.pNext = &MultiviewInfo;
  PassInfo.attachmentCount = 1;
  PassInfo.pAttachments = &Attachment;
  PassInfo.subpassCount = 1;
  PassInfo.pSubpasses = &Subpass;
  VkRenderPass MultiviewPass = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &MultiviewPass),
            VK_SUCCESS);

  VkFramebufferCreateInfo FbInfo{};
  FbInfo.renderPass = MultiviewPass;
  FbInfo.attachmentCount = 1;
  FbInfo.pAttachments = &LayeredView;
  FbInfo.width = Extent;
  FbInfo.height = Extent;
  FbInfo.layers = LayerCount;
  VkFramebuffer MultiviewFb = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateFramebuffer(Device, &FbInfo, nullptr, &MultiviewFb),
            VK_SUCCESS);

  VkShaderModule Vertex = createModule(FullscreenVertexSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;

  VkGraphicsPipelineCreateInfo PipeInfo{};
  PipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  PipeInfo.stageCount = 2;
  PipeInfo.pStages = Stages;
  PipeInfo.pVertexInputState = &VertexInput;
  PipeInfo.pInputAssemblyState = &InputAssembly;
  PipeInfo.pViewportState = &ViewportState;
  PipeInfo.pRasterizationState = &Raster;
  PipeInfo.pMultisampleState = &Multisample;
  PipeInfo.pColorBlendState = &Blend;
  PipeInfo.layout = Layout;
  PipeInfo.renderPass = MultiviewPass;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &PipeInfo,
                                      nullptr, &Pipe),
            VK_SUCCESS);

  VkQueryPoolCreateInfo QueryInfo{};
  QueryInfo.queryType = VK_QUERY_TYPE_OCCLUSION;
  QueryInfo.queryCount = 2;
  VkQueryPool QueryPool = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateQueryPool(Device, &QueryInfo, nullptr, &QueryPool),
            VK_SUCCESS);

  VkCommandBufferBeginInfo BeginInfo{};
  ASSERT_EQ(vkBeginCommandBuffer(Cmd, &BeginInfo), VK_SUCCESS);
  vkCmdResetQueryPool(Cmd, QueryPool, 0, 2);
  VkClearValue ClearValue{};
  ClearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkRenderPassBeginInfo PassBegin{};
  PassBegin.renderPass = MultiviewPass;
  PassBegin.framebuffer = MultiviewFb;
  PassBegin.renderArea = {{0, 0}, {Extent, Extent}};
  PassBegin.clearValueCount = 1;
  PassBegin.pClearValues = &ClearValue;
  vkCmdBeginRenderPass(Cmd, &PassBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  // A single `vkCmdBeginQuery`/`vkCmdEndQuery` pair at query index 0, per
  // this two-view subpass's own multiview query rule, must still populate
  // *both* query index 0 and query index 1.
  vkCmdBeginQuery(Cmd, QueryPool, 0, 0);
  vkCmdDraw(Cmd, 3, 1, 0, 0);
  vkCmdEndQuery(Cmd, QueryPool, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  uint64_t Results[4] = {0, 0, 0, 0};
  EXPECT_EQ(vkGetQueryPoolResults(Device, QueryPool, 0, 2, sizeof(Results),
                                  Results, 2 * sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT |
                                      VK_QUERY_RESULT_WITH_AVAILABILITY_BIT),
            VK_SUCCESS);
  EXPECT_EQ(Results[0], uint64_t(Extent * Extent)) << "view 0's own count";
  EXPECT_EQ(Results[1], 1u) << "view 0's own availability";
  EXPECT_EQ(Results[2], uint64_t(Extent * Extent)) << "view 1's own count";
  EXPECT_EQ(Results[3], 1u) << "view 1's own availability";

  vkDestroyQueryPool(Device, QueryPool, nullptr);
  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyFramebuffer(Device, MultiviewFb, nullptr);
  vkDestroyRenderPass(Device, MultiviewPass, nullptr);
  vkDestroyImageView(Device, LayeredView, nullptr);
  vkDestroyImage(Device, LayeredImage, nullptr);
  vkFreeMemory(Device, LayeredMemory, nullptr);
}

// Roadmap H3: two draw instances, each routed by its own `ViewportIndex`
// (`gl_ViewportIndex`, written from `InstanceIndex` -- see
// `FullscreenVertexWithInstanceViewportSource`) to a different one of two
// dynamic viewport/scissor rectangles set through `vkCmdSetViewportWithCount`
// /`vkCmdSetScissorWithCount`. If `ViewportIndex` were ignored (every
// primitive resolving to viewport/scissor 0, the pre-H3 behavior), the right
// half of the target -- instance 1's own viewport -- would be left at its
// cleared value instead of red.
TEST_F(DrawTest, DynamicViewportWithCountRoutesInstancesToDifferentViewports) {
  VkShaderModule Vertex =
      createModule(FullscreenVertexWithInstanceViewportSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  // Dynamic viewport/scissor: the static arrays below are ignored entirely
  // (see GraphicsPipeline.cpp's `translateViewportState`), but a non-null
  // `pViewportState` is still required.
  VkPipelineViewportStateCreateInfo ViewportState{};
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;
  VkDynamicState Dynamic[2] = {VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
                               VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT};
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 2;
  DynamicInfo.pDynamicStates = Dynamic;

  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.pDynamicState = &DynamicInfo;
  Info.layout = Layout;
  Info.renderPass = Pass;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 0.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  // Viewport/scissor 0: the left half; viewport/scissor 1: the right half.
  std::array<VkViewport, 2> Viewports = {{
      {0.0f, 0.0f, float(Extent) / 2, float(Extent), 0.0f, 1.0f},
      {float(Extent) / 2, 0.0f, float(Extent) / 2, float(Extent), 0.0f, 1.0f},
  }};
  std::array<VkRect2D, 2> Scissors = {{
      {{0, 0}, {Extent / 2, Extent}},
      {{int32_t(Extent / 2), 0}, {Extent / 2, Extent}},
  }};
  vkCmdSetViewportWithCount(Cmd, Viewports.size(), Viewports.data());
  vkCmdSetScissorWithCount(Cmd, Scissors.size(), Scissors.data());
  vkCmdDraw(Cmd, 3, 2, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // Both halves must be red: instance 0 covers the left half through
  // viewport/scissor 0, instance 1 covers the right half through
  // viewport/scissor 1. If `ViewportIndex` were ignored, both instances
  // would resolve to viewport/scissor 0 and the right half would still be
  // the (transparent black) clear color.
  EXPECT_EQ(texel(0, 0)[0], 0xFF) << "left half (viewport 0)";
  EXPECT_EQ(texel(1, 3)[0], 0xFF) << "left half (viewport 0)";
  EXPECT_EQ(texel(2, 0)[0], 0xFF) << "right half (viewport 1)";
  EXPECT_EQ(texel(3, 3)[0], 0xFF) << "right half (viewport 1)";

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

// Roadmap H3: `resolveViewportArrayIndex`'s own out-of-range clamp-or-
// discard behavior (`LayeredRendering.cpp`) end-to-end: a `ViewportIndex`
// written past the bound viewport array's own size discards the whole
// primitive rather than reading/writing out of bounds.
TEST_F(DrawTest, OutOfRangeViewportIndexDiscardsThePrimitive) {
  VkShaderModule Vertex =
      createModule(FullscreenVertexWithInstanceViewportSource);
  VkShaderModule Fragment = createModule(RedFragmentSource);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  // A single-element static viewport/scissor array: instance 0's
  // `ViewportIndex == 0` is in range, instance 1's `ViewportIndex == 1` is
  // not (the array has only one element).
  VkViewport Viewport{0.0f, 0.0f, float(Extent), float(Extent), 0.0f, 1.0f};
  VkRect2D Scissor{{0, 0}, {Extent, Extent}};
  VkPipelineViewportStateCreateInfo ViewportState{};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;

  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.layout = Layout;
  Info.renderPass = Pass;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 0.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  // `firstInstance = 1` makes this single instance's own `gl_InstanceIndex`
  // (and so its `ViewportIndex` output) equal to 1, which is out of range
  // for the pipeline's one-element static viewport/scissor array above.
  vkCmdDraw(Cmd, 3, 1, 0, 1);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // The whole draw's only primitive is out of range and must be discarded
  // entirely, leaving every texel at the (transparent black) clear color
  // rather than crashing or reading/writing past the one-element
  // viewport/scissor array.
  EXPECT_EQ(texel(0, 0)[0], 0x00);
  EXPECT_EQ(texel(3, 3)[0], 0x00);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

// Roadmap H3a: `gl_ViewportIndex` read back as a *fragment*-shader input
// (rather than only written as a vertex-shader output, as roadmap H3's own
// tests above exercise). Before this milestone, any fragment-stage entry
// function using a bound resource reached pipeline creation with no
// `feme.signature` metadata attached at all (SPIRVResourceLowering.cpp's
// `addResourceEnvParams` silently dropped it), and even once metadata
// survived, `FragmentWrapper.cpp` had no case for
// `SignatureSystemValue::ViewportArrayIndex` and `Executor.cpp` never
// threaded the resolved viewport index into the per-lane fragment
// invocation. Two draw instances, each routed by its own `ViewportIndex` to
// a different one of two viewports (mirroring
// `DynamicViewportWithCountRoutesInstancesToDifferentViewports` above), but
// this time the *fragment* shader reads `gl_ViewportIndex` back and selects
// its output color accordingly -- if the fragment input read failed
// (pipeline creation failure) or silently read zero (metadata/wiring loss),
// every pixel would be red instead of the right half being blue.
TEST_F(DrawTest, FragmentShaderReadsBackViewportIndex) {
  VkShaderModule Vertex =
      createModule(FullscreenVertexWithInstanceViewportSource);
  VkShaderModule Fragment = createModule(ViewportIndexFragmentSource);

  VkPipelineShaderStageCreateInfo Stages[2]{};
  Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Stages[0].module = Vertex;
  Stages[0].pName = "main";
  Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Stages[1].module = Fragment;
  Stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo ViewportState{};
  VkPipelineRasterizationStateCreateInfo Raster{};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  VkPipelineMultisampleStateCreateInfo Multisample{};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  BlendAttachment.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo Blend{};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;
  VkDynamicState Dynamic[2] = {VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
                               VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT};
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 2;
  DynamicInfo.pDynamicStates = Dynamic;

  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = Stages;
  Info.pVertexInputState = &VertexInput;
  Info.pInputAssemblyState = &InputAssembly;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.pDynamicState = &DynamicInfo;
  Info.layout = Layout;
  Info.renderPass = Pass;

  VkPipeline Pipe = VK_NULL_HANDLE;
  // Prior to the H3a fix this failed with VK_ERROR_INITIALIZATION_FAILED
  // ("fragment stage wrapper requires attached feme.signature metadata").
  ASSERT_EQ(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr,
                                      &Pipe),
            VK_SUCCESS);

  beginRenderPass(VkClearColorValue{{0.0f, 0.0f, 0.0f, 0.0f}});
  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipe);
  std::array<VkViewport, 2> Viewports = {{
      {0.0f, 0.0f, float(Extent) / 2, float(Extent), 0.0f, 1.0f},
      {float(Extent) / 2, 0.0f, float(Extent) / 2, float(Extent), 0.0f, 1.0f},
  }};
  std::array<VkRect2D, 2> Scissors = {{
      {{0, 0}, {Extent / 2, Extent}},
      {{int32_t(Extent / 2), 0}, {Extent / 2, Extent}},
  }};
  vkCmdSetViewportWithCount(Cmd, Viewports.size(), Viewports.data());
  vkCmdSetScissorWithCount(Cmd, Scissors.size(), Scissors.data());
  vkCmdDraw(Cmd, 3, 2, 0, 0);
  vkCmdEndRenderPass(Cmd);
  ASSERT_EQ(vkEndCommandBuffer(Cmd), VK_SUCCESS);
  ASSERT_EQ(submit(), VK_SUCCESS);

  // Left half (viewport/instance 0): fragment shader reads back
  // gl_ViewportIndex == 0 and selects red. Right half (viewport/instance 1):
  // reads back gl_ViewportIndex == 1 and selects blue.
  EXPECT_EQ(texel(0, 0)[0], 0xFF) << "left half is red (viewport 0)";
  EXPECT_EQ(texel(0, 0)[2], 0x00) << "left half is red (viewport 0)";
  EXPECT_EQ(texel(1, 3)[0], 0xFF) << "left half is red (viewport 0)";
  EXPECT_EQ(texel(2, 0)[2], 0xFF) << "right half is blue (viewport 1)";
  EXPECT_EQ(texel(2, 0)[0], 0x00) << "right half is blue (viewport 1)";
  EXPECT_EQ(texel(3, 3)[2], 0xFF) << "right half is blue (viewport 1)";

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

} // namespace
