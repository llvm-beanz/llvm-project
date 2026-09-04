//===- GraphicsPipelineTest.cpp - vkCreateGraphicsPipelines tests -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// (V6) Covers graphics stage compilation and pipeline state translation:
// real SPIR-V vertex/fragment modules compiled into `feme::cpu::
// CompiledStage`s, their cross-stage interface validated against the core
// reflection, and every state combination with no implemented path rejected
// at creation rather than at draw time (see "Graphics pipeline state" in
// feme/docs/FeMeVulkanDesign.md).
//
//===----------------------------------------------------------------------===//

#define VK_NO_PROTOTYPES
#include "GraphicsPipeline.h"
#include "EntryPoints.h"
#include "Icd.h"
#include "Objects.h"
#include "RenderPass.h"

#include "feme/Graphics/Patch.h"

#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Target/SPIRV/Serialization.h"

#include "gtest/gtest.h"

#include <array>
#include <string>
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

/// A vertex stage selecting one of three hard-coded oversized-triangle
/// corners from `gl_VertexIndex`, exactly like the executor's own
/// `feme-render` triangle fixture -- no vertex buffer needed.
constexpr llvm::StringLiteral VertexSource = R"mlir(
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

/// A fragment stage writing solid red to location 0 (SV_Target0).
constexpr llvm::StringLiteral FragmentSource = R"mlir(
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

/// (Roadmap H2b) A fragment stage with no *color* output at all -- only a
/// `gl_FragDepth` write -- the shape `dEQP-VK.multiview.depth_without_
/// fragment_shader`'s own depth-only pipeline uses.
constexpr llvm::StringLiteral NoColorOutputFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @depth built_in("FragDepth") : !spirv.ptr<f32, Output>
  spirv.func @main() -> () "None" {
    %d = spirv.Constant 0.5 : f32
    %p = spirv.mlir.addressof @depth : !spirv.ptr<f32, Output>
    spirv.Store "Output" %p, %d : f32
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @depth
  spirv.ExecutionMode @main "OriginUpperLeft"
  spirv.ExecutionMode @main "DepthReplacing"
}
)mlir";

/// (Roadmap H4b) A tessellation-control entry point declaring 3 output
/// control points (`OutputVertices`), writing its own control-point
/// `Position`, then splitting at a real SPIR-V-imported
/// `spirv.ControlBarrier` (roadmap H4a's `splitTessellationControlEntry`,
/// see its own comment and this file's `CanonicalizeStageTest` sibling for
/// why the mangled-call-lowered form matters) into a patch-constant phase
/// writing one `patch`-decorated output.
constexpr llvm::StringLiteral TessControlSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Tessellation], []> {
  spirv.GlobalVariable @out_pos built_in("Position") : !spirv.ptr<vector<4xf32>, Output>
  spirv.GlobalVariable @patch_out {location = 0 : i32, patch} : !spirv.ptr<f32, Output>
  spirv.func @main() -> () "None" {
    %p = spirv.Constant dense<[0.0, 0.0, 0.0, 1.0]> : vector<4xf32>
    %posp = spirv.mlir.addressof @out_pos : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %posp, %p : vector<4xf32>
    spirv.ControlBarrier <Workgroup>, <Workgroup>, <AcquireRelease|WorkgroupMemory>
    %f = spirv.Constant 1.000000e+00 : f32
    %outp = spirv.mlir.addressof @patch_out : !spirv.ptr<f32, Output>
    spirv.Store "Output" %outp, %f : f32
    spirv.Return
  }
  spirv.EntryPoint "TessellationControl" @main, @out_pos, @patch_out
  spirv.ExecutionMode @main "OutputVertices", 3
}
)mlir";

/// (Roadmap H4b) A tessellation-evaluation entry point declaring a
/// triangle domain, reading the control stage's `patch`-decorated output
/// back, and writing its own `Position`.
constexpr llvm::StringLiteral TessEvalSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Tessellation], []> {
  spirv.GlobalVariable @patch_in {location = 0 : i32, patch} : !spirv.ptr<f32, Input>
  spirv.GlobalVariable @out_pos built_in("Position") : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %inp = spirv.mlir.addressof @patch_in : !spirv.ptr<f32, Input>
    %f = spirv.Load "Input" %inp : f32
    %v = spirv.CompositeConstruct %f, %f, %f, %f : (f32, f32, f32, f32) -> vector<4xf32>
    %posp = spirv.mlir.addressof @out_pos : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %posp, %v : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "TessellationEvaluation" @main, @patch_in, @out_pos
  spirv.ExecutionMode @main "Triangles"
  spirv.ExecutionMode @main "SpacingFractionalOdd"
  spirv.ExecutionMode @main "VertexOrderCcw"
}
)mlir";

/// (Roadmap H4d) A tessellation-control entry point writing more than one
/// element of a *bare* (non-block) array-typed `BuiltIn` output --
/// `gl_TessLevelOuter`'s own `[4 x f32]` shape, exactly what every real
/// `dEQP-VK.tessellation.*` control shader writes -- after the same
/// `spirv.ControlBarrier` split `TessControlSource` above exercises. Before
/// this milestone's fix, only the first element (byte offset 0) of such an
/// array ever got rewritten into a `feme.stage.output.store`; every other
/// element's store was left referencing the still-`external`,
/// never-defined SPIR-V global directly, an unresolvable symbol at JIT
/// link time (`LLJIT`'s own "Symbols not found: [ gl_TessLevelOuter ]",
/// the exact defect that rejected `dEQP-VK.tessellation.winding.*`).
constexpr llvm::StringLiteral TessControlMultiElementArraySource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Tessellation], []> {
  spirv.GlobalVariable @out_pos built_in("Position") : !spirv.ptr<vector<4xf32>, Output>
  spirv.GlobalVariable @tess_outer built_in("TessLevelOuter") {patch} : !spirv.ptr<!spirv.array<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %p = spirv.Constant dense<[0.0, 0.0, 0.0, 1.0]> : vector<4xf32>
    %posp = spirv.mlir.addressof @out_pos : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %posp, %p : vector<4xf32>
    spirv.ControlBarrier <Workgroup>, <Workgroup>, <AcquireRelease|WorkgroupMemory>
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %f = spirv.Constant 1.000000e+00 : f32
    %outerp = spirv.mlir.addressof @tess_outer : !spirv.ptr<!spirv.array<4xf32>, Output>
    %e0 = spirv.AccessChain %outerp[%c0] : !spirv.ptr<!spirv.array<4xf32>, Output>, i32 -> !spirv.ptr<f32, Output>
    spirv.Store "Output" %e0, %f : f32
    %e1 = spirv.AccessChain %outerp[%c1] : !spirv.ptr<!spirv.array<4xf32>, Output>, i32 -> !spirv.ptr<f32, Output>
    spirv.Store "Output" %e1, %f : f32
    spirv.Return
  }
  spirv.EntryPoint "TessellationControl" @main, @out_pos, @tess_outer
  spirv.ExecutionMode @main "OutputVertices", 3
}
)mlir";

/// (Roadmap H9c) A tessellation-control entry point guarding its
/// tessellation-factor write with `if (gl_InvocationID == 0)` -- the real
/// shape `glslang`/most GLSL-to-SPIR-V compilers emit for a patch-constant
/// function, since every one of a patch's `OutputVertices`-many
/// invocations reaches the post-barrier code and executes it, and the
/// source guards so only one of them actually stores the (shared, once-
/// per-patch) result. `gl_InvocationID`'s own `feme.stage.input.load` is
/// `WaveTTIImpl::getValueUniformity`'s `NeverUniform` case (see
/// WaveUniformity.cpp) regardless of which phase reads it, so
/// `feme::cpu::LinearizePass` cannot statically prove this branch uniform
/// even though it always resolves the same way on every lane once
/// `PatchConstantWrapper.cpp`'s `lowerPatchConstantSystemValue` lowers it
/// -- it rewrites the guarded `feme.stage.output.store` into a masked
/// `feme.cpu.masked.output.store` call.
constexpr llvm::StringLiteral TessControlMaskedPatchConstantStoreSource =
    R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Tessellation], []> {
  spirv.GlobalVariable @out_pos built_in("Position") : !spirv.ptr<vector<4xf32>, Output>
  spirv.GlobalVariable @invocation_id built_in("InvocationId") : !spirv.ptr<i32, Input>
  spirv.GlobalVariable @tess_outer built_in("TessLevelOuter") {patch} : !spirv.ptr<!spirv.array<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %p = spirv.Constant dense<[0.0, 0.0, 0.0, 1.0]> : vector<4xf32>
    %posp = spirv.mlir.addressof @out_pos : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %posp, %p : vector<4xf32>
    spirv.ControlBarrier <Workgroup>, <Workgroup>, <AcquireRelease|WorkgroupMemory>
    %idp = spirv.mlir.addressof @invocation_id : !spirv.ptr<i32, Input>
    %id = spirv.Load "Input" %idp : i32
    %c0 = spirv.Constant 0 : i32
    %cond = spirv.IEqual %id, %c0 : i32
    spirv.BranchConditional %cond, ^then, ^end
  ^then:
    %f = spirv.Constant 1.000000e+00 : f32
    %outerp = spirv.mlir.addressof @tess_outer : !spirv.ptr<!spirv.array<4xf32>, Output>
    %e0 = spirv.AccessChain %outerp[%c0] : !spirv.ptr<!spirv.array<4xf32>, Output>, i32 -> !spirv.ptr<f32, Output>
    spirv.Store "Output" %e0, %f : f32
    spirv.Branch ^end
  ^end:
    spirv.Return
  }
  spirv.EntryPoint "TessellationControl" @main, @out_pos, @invocation_id, @tess_outer
  spirv.ExecutionMode @main "OutputVertices", 3
}
)mlir";

/// (Roadmap H9c) The real, *barrierless* shape every genuine `dEQP-VK.
/// tessellation.*`/pipeline-statistics tessellation-control shader
/// actually compiles to (see e.g.
/// `vktQueryPoolStatisticsTests.cpp`'s own tessellation-control source,
/// which never calls `barrier()` at all): an ordinary per-control-point
/// `Position` write is *not* separated from the `if (gl_InvocationID ==
/// 0)`-guarded, `patch`-decorated tessellation-factor write by any
/// `spirv.ControlBarrier` -- unlike `TessControlMaskedPatchConstantStore
/// Source` above, whose barrier makes `splitTessellationControlEntry`
/// split out a real `.patchconstant` clone before either store is ever
/// classified. Legal per SPIR-V (no invocation ever reads another's own
/// output here, so no synchronization is actually needed), this shape hits
/// `splitBarrierlessTessellationControlEntry`'s own `isPatchConstantOnly
/// Entry` check instead, which -- seeing this same function's own
/// unconditional `Position` store is *not* patch-frequency -- refuses to
/// split at all, leaving the whole, still-mixed function to be
/// canonicalized as a single `SPIRVCanonicalPhase::HullControlPoint`
/// phase.
constexpr llvm::StringLiteral TessControlBarrierlessMixedStoreSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Tessellation], []> {
  spirv.GlobalVariable @out_pos built_in("Position") : !spirv.ptr<vector<4xf32>, Output>
  spirv.GlobalVariable @invocation_id built_in("InvocationId") : !spirv.ptr<i32, Input>
  spirv.GlobalVariable @tess_outer built_in("TessLevelOuter") {patch} : !spirv.ptr<!spirv.array<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %idp = spirv.mlir.addressof @invocation_id : !spirv.ptr<i32, Input>
    %id = spirv.Load "Input" %idp : i32
    %c0 = spirv.Constant 0 : i32
    %cond = spirv.IEqual %id, %c0 : i32
    spirv.BranchConditional %cond, ^then, ^end
  ^then:
    %f = spirv.Constant 1.000000e+00 : f32
    %outerp = spirv.mlir.addressof @tess_outer : !spirv.ptr<!spirv.array<4xf32>, Output>
    %e0 = spirv.AccessChain %outerp[%c0] : !spirv.ptr<!spirv.array<4xf32>, Output>, i32 -> !spirv.ptr<f32, Output>
    spirv.Store "Output" %e0, %f : f32
    spirv.Branch ^end
  ^end:
    %p = spirv.Constant dense<[0.0, 0.0, 0.0, 1.0]> : vector<4xf32>
    %posp = spirv.mlir.addressof @out_pos : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %posp, %p : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "TessellationControl" @main, @out_pos, @invocation_id, @tess_outer
  spirv.ExecutionMode @main "OutputVertices", 3
}
)mlir";

/// (Roadmap H9c) The real, barrierless shape every genuine `dEQP-VK.
/// query_pool.statistics_query.clipping_invocations.*_tessellation*`
/// tessellation-control shader actually compiles to: a *dynamically*
/// vertex-indexed per-vertex output store (`out_color[gl_InvocationID] =
/// ...`, mirroring `vktQueryPoolStatisticsTests.cpp`'s own `out_color[gl_
/// InvocationID] = in_color[gl_InvocationID];`) alongside the same `if
/// (gl_InvocationID == 0)`-guarded, `patch`-decorated tessellation-factor
/// write `TessControlBarrierlessMixedStoreSource` above already covers --
/// unlike that source's own unconditional, non-array `Position` write
/// (constant-offset, resolved by `getStageIOBaseAndOffset`), this one is
/// exactly the shape `getDynamicVertexIndexedAccess` resolves instead,
/// which `isPatchConstantOnlyEntry`'s own scan does not (yet) call.
constexpr llvm::StringLiteral
    TessControlBarrierlessDynamicVertexIndexedMixedStoreSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Tessellation], []> {
  spirv.GlobalVariable @out_color {location = 0 : i32} : !spirv.ptr<!spirv.array<3xvector<4xf32>>, Output>
  spirv.GlobalVariable @invocation_id built_in("InvocationId") : !spirv.ptr<i32, Input>
  spirv.GlobalVariable @tess_outer built_in("TessLevelOuter") {patch} : !spirv.ptr<!spirv.array<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %idp = spirv.mlir.addressof @invocation_id : !spirv.ptr<i32, Input>
    %id = spirv.Load "Input" %idp : i32
    %c0 = spirv.Constant 0 : i32
    %cond = spirv.IEqual %id, %c0 : i32
    spirv.BranchConditional %cond, ^then, ^end
  ^then:
    %f = spirv.Constant 1.000000e+00 : f32
    %outerp = spirv.mlir.addressof @tess_outer : !spirv.ptr<!spirv.array<4xf32>, Output>
    %e0 = spirv.AccessChain %outerp[%c0] : !spirv.ptr<!spirv.array<4xf32>, Output>, i32 -> !spirv.ptr<f32, Output>
    spirv.Store "Output" %e0, %f : f32
    spirv.Branch ^end
  ^end:
    %c = spirv.Constant dense<[0.0, 0.0, 1.0, 1.0]> : vector<4xf32>
    %colorp = spirv.mlir.addressof @out_color : !spirv.ptr<!spirv.array<3xvector<4xf32>>, Output>
    %ce = spirv.AccessChain %colorp[%id] : !spirv.ptr<!spirv.array<3xvector<4xf32>>, Output>, i32 -> !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %ce, %c : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "TessellationControl" @main, @out_color, @invocation_id, @tess_outer
  spirv.ExecutionMode @main "OutputVertices", 3
}
)mlir";

/// (Roadmap H4h) A genuinely-empty vertex stage -- no stage-IO globals at
/// all, `void main (void) {}` -- exactly `dEQP-VK.tessellation.winding.*`'s
/// own real vertex shader, legal whenever a tessellation-evaluation stage
/// computes its own `SV_Position`/`gl_Position` purely from `gl_TessCoord`
/// and never reads a per-vertex output back via `gl_in[]`.
constexpr llvm::StringLiteral EmptyVertexSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @main() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @main
}
)mlir";

/// (Roadmap H5e) A geometry entry point declaring a triangle input
/// primitive and a triangle-strip output of up to 3 vertices, writing its
/// own `Position` -- no `spirv.EmitVertex`/`spirv.EndPrimitive` calls,
/// since `ConvertSPIRVToLLVMPass`/`SPIRVToLLVMPatterns` do not lower those
/// ops yet (a gap this milestone's own report spins off as a follow-up
/// row rather than fixing here). This is enough to exercise every
/// Vulkan-layer acceptance/translation path H5e adds: compiling the
/// module into a `feme::ShaderStage::Geometry` `CompiledStage`, reading
/// back its `feme::graphics::GeometryState`, and feeding both into
/// `graphics::GraphicsPipeline::setGeometryStage`.
constexpr llvm::StringLiteral GeometrySource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Geometry], []> {
  spirv.GlobalVariable @out_pos built_in("Position") : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %p = spirv.Constant dense<[0.0, 0.0, 0.0, 1.0]> : vector<4xf32>
    %posp = spirv.mlir.addressof @out_pos : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %posp, %p : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Geometry" @main, @out_pos
  spirv.ExecutionMode @main "Triangles"
  spirv.ExecutionMode @main "OutputTriangleStrip"
  spirv.ExecutionMode @main "OutputVertices", 3
}
)mlir";

/// (Roadmap H5e-b) A geometry entry point that emits no vertices at all --
/// `void main(void) {}`, calling neither `spirv.EmitVertex` nor
/// `spirv.EndPrimitive` -- exactly `dEQP-VK.geometry.emit.*_emit_0_end_0`'s
/// degenerate shape. Still declares its input/output primitive class
/// execution modes (SPIR-V requires them regardless of whether the entry
/// point's body does anything), but its interface lists no globals at
/// all: SPIR-V only lists an entry point's *used* interface variables, and
/// this one uses none.
constexpr llvm::StringLiteral EmptyGeometrySource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Geometry], []> {
  spirv.func @main() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "Geometry" @main
  spirv.ExecutionMode @main "Triangles"
  spirv.ExecutionMode @main "OutputTriangleStrip"
  spirv.ExecutionMode @main "OutputVertices", 1
}
)mlir";

/// A fragment stage reading a `vec4` varying at location 0 and writing it
/// straight to `SV_Target0` -- the shape needed to exercise
/// `AcceptsGeometryStageThatNeverEmits`'s own fragment-input-linkage
/// relaxation (`dEQP-VK.geometry.emit.*_emit_0_end_0` reads back its own
/// `v_frag_FragColor` varying this way).
constexpr llvm::StringLiteral VaryingFragmentSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @varying {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Input>
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %vp = spirv.mlir.addressof @varying : !spirv.ptr<vector<4xf32>, Input>
    %v = spirv.Load "Input" %vp : vector<4xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p, %v : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @varying, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir";

/// (roadmap H6f) A minimal mesh entry point: declares its output topology/
/// count execution modes (`OutputTrianglesEXT`/`OutputVertices`/
/// `OutputPrimitivesEXT`) and workgroup size (`LocalSize`, mirroring a
/// compute entry's own), but emits nothing (no `spirv.EmitMeshTasksEXT`/
/// per-vertex writes -- roadmap H6h/H6i is what would make it emit real
/// geometry). Enough to exercise `vkCreateGraphicsPipelines` accepting a
/// mesh pipeline at all, mirroring `EmptyGeometrySource`'s own "declares
/// its shape, writes nothing" role for the geometry stage.
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

/// (roadmap H6f) A minimal task entry point: no `EmitMeshTasksEXT` call
/// (so it never actually drives the mesh stage -- mirrors `MeshSource`'s
/// own "declared but empty" role), just a workgroup size.
constexpr llvm::StringLiteral TaskSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [MeshShadingEXT], [SPV_EXT_mesh_shader]> {
  spirv.func @main() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "TaskEXT" @main
  spirv.ExecutionMode @main "LocalSize", 1, 1, 1
}
)mlir";

class GraphicsPipelineTest : public ::testing::Test {
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

    // (roadmap C4c) A second render pass, identical but for a depth
    // attachment: used only by tests exercising a dynamic depth/stencil
    // state, which -- unlike the fixture's other tests -- needs one
    // declared for the pipeline to legally test/write into.
    VkAttachmentDescription DepthAttachment{};
    DepthAttachment.format = VK_FORMAT_D32_SFLOAT;
    DepthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    DepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    DepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkAttachmentReference DepthRef{
        1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentDescription DepthAttachments[2] = {Attachment, DepthAttachment};
    VkSubpassDescription DepthSubpass{};
    DepthSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    DepthSubpass.colorAttachmentCount = 1;
    DepthSubpass.pColorAttachments = &ColorRef;
    DepthSubpass.pDepthStencilAttachment = &DepthRef;
    VkRenderPassCreateInfo DepthPassInfo{};
    DepthPassInfo.attachmentCount = 2;
    DepthPassInfo.pAttachments = DepthAttachments;
    DepthPassInfo.subpassCount = 1;
    DepthPassInfo.pSubpasses = &DepthSubpass;
    ASSERT_EQ(
        vkCreateRenderPass(Device, &DepthPassInfo, nullptr, &PassWithDepth),
        VK_SUCCESS);
  }

  void TearDown() override {
    vkDestroyRenderPass(Device, Pass, nullptr);
    vkDestroyRenderPass(Device, PassWithDepth, nullptr);
    vkDestroyPipelineLayout(Device, Layout, nullptr);
    vkDestroyDevice(Device, nullptr);
    vkDestroyInstance(Instance, nullptr);
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

  /// A fully populated `VkGraphicsPipelineCreateInfo` over the fixture's
  /// render pass, with every state block at its supported default. The
  /// caller may mutate the state structures (kept alive as members) before
  /// calling `create`.
  VkGraphicsPipelineCreateInfo makeCreateInfo(VkShaderModule Vertex,
                                              VkShaderModule Fragment) {
    Stages[0] = {};
    Stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    Stages[0].module = Vertex;
    Stages[0].pName = "main";
    Stages[1] = {};
    Stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    Stages[1].module = Fragment;
    Stages[1].pName = "main";

    VertexInput = {};
    InputAssembly = {};
    InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    Viewport = {0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
    Scissor = {{0, 0}, {4, 4}};
    ViewportState = {};
    ViewportState.viewportCount = 1;
    ViewportState.pViewports = &Viewport;
    ViewportState.scissorCount = 1;
    ViewportState.pScissors = &Scissor;
    Raster = {};
    Raster.cullMode = VK_CULL_MODE_NONE;
    Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Raster.polygonMode = VK_POLYGON_MODE_FILL;
    Raster.lineWidth = 1.0f;
    Multisample = {};
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    BlendAttachment = {};
    BlendAttachment.colorWriteMask = 0xF;
    Blend = {};
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
    return Info;
  }

  VkResult create(const VkGraphicsPipelineCreateInfo &Info, VkPipeline &Out,
                  VkPipelineCache Cache = VK_NULL_HANDLE) {
    return vkCreateGraphicsPipelines(Device, Cache, 1, &Info, nullptr, &Out);
  }

  /// (Roadmap H4b) `makeCreateInfo`'s tessellation-enabled sibling: a
  /// four-stage `VK_PRIMITIVE_TOPOLOGY_PATCH_LIST` pipeline with
  /// `patchControlPoints` set to 3, over `TessStages` rather than `Stages`
  /// (so a caller wanting both a tessellating and a non-tessellating
  /// pipeline alive at once, e.g. to compare cache keys, can).
  VkGraphicsPipelineCreateInfo
  makeTessellationCreateInfo(VkShaderModule Vertex, VkShaderModule TessControl,
                             VkShaderModule TessEval, VkShaderModule Fragment) {
    VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
    TessStages[0] = Stages[0];
    TessStages[1] = {};
    TessStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    TessStages[1].stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    TessStages[1].module = TessControl;
    TessStages[1].pName = "main";
    TessStages[2] = {};
    TessStages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    TessStages[2].stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    TessStages[2].module = TessEval;
    TessStages[2].pName = "main";
    TessStages[3] = Stages[1];

    InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    Tessellation = {};
    Tessellation.sType =
        VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    Tessellation.patchControlPoints = 3;

    Info.stageCount = 4;
    Info.pStages = TessStages;
    Info.pTessellationState = &Tessellation;
    return Info;
  }

  /// (Roadmap H5e) `makeCreateInfo`'s geometry-enabled sibling: a
  /// three-stage (vertex/geometry/fragment) pipeline over `GeomStages`
  /// rather than `Stages`. The base's own `TriangleList` topology is left
  /// unchanged, since a geometry stage does not by itself require an
  /// adjacency topology -- only the converse (an adjacency topology
  /// requires a bound geometry stage) is enforced.
  VkGraphicsPipelineCreateInfo makeGeometryCreateInfo(VkShaderModule Vertex,
                                                      VkShaderModule Geometry,
                                                      VkShaderModule Fragment) {
    VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
    GeomStages[0] = Stages[0];
    GeomStages[1] = {};
    GeomStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    GeomStages[1].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
    GeomStages[1].module = Geometry;
    GeomStages[1].pName = "main";
    GeomStages[2] = Stages[1];

    Info.stageCount = 3;
    Info.pStages = GeomStages;
    return Info;
  }

  /// (roadmap H6f) `makeCreateInfo`'s mesh-enabled sibling: a mesh pipeline
  /// has no vertex stage and no vertex-input/input-assembly state at all
  /// (`translateFixedFunctionState`'s own check), so this does not build
  /// on `makeCreateInfo` the way `makeTessellationCreateInfo`/
  /// `makeGeometryCreateInfo` do -- it assembles a
  /// `VkGraphicsPipelineCreateInfo` from scratch instead, over
  /// `MeshStages`. \p Task is `VK_NULL_HANDLE` for a mesh pipeline with no
  /// task stage (legal -- see `GraphicsPipeline.h`'s `hasTaskStage`), in
  /// which case only the mesh and fragment stages are named.
  VkGraphicsPipelineCreateInfo
  makeMeshCreateInfo(VkShaderModule Mesh, VkShaderModule Fragment,
                     VkShaderModule Task = VK_NULL_HANDLE) {
    uint32_t StageCount = 0;
    if (Task != VK_NULL_HANDLE) {
      MeshStages[StageCount] = {};
      MeshStages[StageCount].sType =
          VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      MeshStages[StageCount].stage = VK_SHADER_STAGE_TASK_BIT_EXT;
      MeshStages[StageCount].module = Task;
      MeshStages[StageCount].pName = "main";
      ++StageCount;
    }
    MeshStages[StageCount] = {};
    MeshStages[StageCount].sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    MeshStages[StageCount].stage = VK_SHADER_STAGE_MESH_BIT_EXT;
    MeshStages[StageCount].module = Mesh;
    MeshStages[StageCount].pName = "main";
    ++StageCount;
    MeshStages[StageCount] = {};
    MeshStages[StageCount].sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    MeshStages[StageCount].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    MeshStages[StageCount].module = Fragment;
    MeshStages[StageCount].pName = "main";
    ++StageCount;

    Viewport = {0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
    Scissor = {{0, 0}, {4, 4}};
    ViewportState = {};
    ViewportState.viewportCount = 1;
    ViewportState.pViewports = &Viewport;
    ViewportState.scissorCount = 1;
    ViewportState.pScissors = &Scissor;
    Raster = {};
    Raster.cullMode = VK_CULL_MODE_NONE;
    Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Raster.polygonMode = VK_POLYGON_MODE_FILL;
    Raster.lineWidth = 1.0f;
    Multisample = {};
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    BlendAttachment = {};
    BlendAttachment.colorWriteMask = 0xF;
    Blend = {};
    Blend.attachmentCount = 1;
    Blend.pAttachments = &BlendAttachment;

    VkGraphicsPipelineCreateInfo Info{};
    Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    Info.stageCount = StageCount;
    Info.pStages = MeshStages;
    // (roadmap H6g-b) A mesh pipeline declares neither by default here;
    // both are spec-ignored (not required to be null) for a mesh
    // pipeline -- see `AcceptsMeshPipelineWith{VertexInput,InputAssembly}
    // State` below for the non-null case.
    Info.pVertexInputState = nullptr;
    Info.pInputAssemblyState = nullptr;
    Info.pViewportState = &ViewportState;
    Info.pRasterizationState = &Raster;
    Info.pMultisampleState = &Multisample;
    Info.pColorBlendState = &Blend;
    Info.layout = Layout;
    Info.renderPass = Pass;
    return Info;
  }

  VkInstance Instance = VK_NULL_HANDLE;
  VkPhysicalDevice Physical = VK_NULL_HANDLE;
  VkDevice Device = VK_NULL_HANDLE;
  VkPipelineLayout Layout = VK_NULL_HANDLE;
  VkRenderPass Pass = VK_NULL_HANDLE;
  VkRenderPass PassWithDepth = VK_NULL_HANDLE;

  VkPipelineShaderStageCreateInfo Stages[2]{};
  VkPipelineShaderStageCreateInfo TessStages[4]{};
  VkPipelineShaderStageCreateInfo GeomStages[3]{};
  VkPipelineShaderStageCreateInfo MeshStages[3]{};
  VkPipelineTessellationStateCreateInfo Tessellation{};
  VkPipelineVertexInputStateCreateInfo VertexInput{};
  VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
  VkViewport Viewport{};
  VkRect2D Scissor{};
  VkPipelineViewportStateCreateInfo ViewportState{};
  VkPipelineRasterizationStateCreateInfo Raster{};
  VkPipelineMultisampleStateCreateInfo Multisample{};
  VkPipelineColorBlendAttachmentState BlendAttachment{};
  VkPipelineColorBlendStateCreateInfo Blend{};
};

TEST_F(GraphicsPipelineTest, CompilesVertexAndFragmentStages) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  ASSERT_NE(Vertex, VK_NULL_HANDLE);
  ASSERT_NE(Fragment, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Obj = fromHandle<Pipeline>(Pipe);
  ASSERT_EQ(Obj->kind(), Pipeline::Kind::Graphics);
  auto *Graphics = static_cast<GraphicsPipeline *>(Obj);
  EXPECT_EQ(Graphics->colorAttachmentCount(), 1u);
  EXPECT_EQ(Graphics->sampleCount(), 1u);
  EXPECT_FALSE(Graphics->needsDepthAttachment());
  EXPECT_EQ(Graphics->vertexStage().getStage(), feme::ShaderStage::Vertex);
  EXPECT_EQ(Graphics->fragmentStage().getStage(), feme::ShaderStage::Fragment);

  // The executor pipeline this builds per draw carries the translated
  // state, one blend state per color attachment.
  DynamicGraphicsState Dynamic;
  feme::graphics::GraphicsPipeline Executor =
      Graphics->buildExecutorPipeline(Dynamic);
  EXPECT_EQ(Executor.getTopology(),
            feme::graphics::PrimitiveTopology::TriangleList);
  EXPECT_EQ(Executor.getColorBlends().size(), 1u);
  EXPECT_EQ(Graphics->resolveViewport(Dynamic)[0].Width, 4.0f);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap F9 (`VK_EXT_pipeline_protected_access`): the extension's two
/// restriction bits apply to a graphics pipeline exactly like a compute one
/// (`Pipeline` is their common base -- see `PipelineTest.
/// Accepts{No,ProtectedAccessOnly}CreateFlag`'s compute-side coverage) --
/// creation records the flag verbatim on `Pipeline::createFlags`.
TEST_F(GraphicsPipelineTest, RecordsProtectedAccessCreateFlags) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  ASSERT_NE(Vertex, VK_NULL_HANDLE);
  ASSERT_NE(Fragment, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Info.flags = VK_PIPELINE_CREATE_PROTECTED_ACCESS_ONLY_BIT;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  EXPECT_EQ(fromHandle<Pipeline>(Pipe)->createFlags() &
                VK_PIPELINE_CREATE_PROTECTED_ACCESS_ONLY_BIT,
            static_cast<VkPipelineCreateFlags>(
                VK_PIPELINE_CREATE_PROTECTED_ACCESS_ONLY_BIT));

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap E19 (`VK_EXT_pipeline_creation_feedback`): two stages (vertex +
/// fragment) get two feedback slots, both `VALID_BIT`-only on a cache
/// miss, matching `PipelineTest.ReportsPipelineCreationFeedback`'s compute
/// counterpart.
TEST_F(GraphicsPipelineTest, ReportsPipelineCreationFeedback) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  ASSERT_NE(Vertex, VK_NULL_HANDLE);
  ASSERT_NE(Fragment, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkPipelineCreationFeedback Feedback{};
  VkPipelineCreationFeedback StageFeedbacks[2]{};
  VkPipelineCreationFeedbackCreateInfo FeedbackInfo{};
  FeedbackInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO;
  FeedbackInfo.pPipelineCreationFeedback = &Feedback;
  FeedbackInfo.pipelineStageCreationFeedbackCount = 2;
  FeedbackInfo.pPipelineStageCreationFeedbacks = StageFeedbacks;
  Info.pNext = &FeedbackInfo;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);

  EXPECT_EQ(Feedback.flags, static_cast<VkPipelineCreationFeedbackFlags>(
                                VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT));
  for (const VkPipelineCreationFeedback &StageFeedback : StageFeedbacks)
    EXPECT_EQ(StageFeedback.flags,
              static_cast<VkPipelineCreationFeedbackFlags>(
                  VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT));

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

// roadmap C4: `mapTopology` beyond `TriangleList`/`TriangleStrip`. Every
// `VkPrimitiveTopology` this milestone's executor implements
// (point/line/line-strip/triangle-fan) creates successfully and translates
// to the matching `feme::graphics::PrimitiveTopology`.
TEST_F(GraphicsPipelineTest, AcceptsEveryImplementedTopology) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  static constexpr std::pair<VkPrimitiveTopology,
                             feme::graphics::PrimitiveTopology>
      Cases[] = {
          {VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
           feme::graphics::PrimitiveTopology::PointList},
          {VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
           feme::graphics::PrimitiveTopology::LineList},
          {VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
           feme::graphics::PrimitiveTopology::LineStrip},
          {VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
           feme::graphics::PrimitiveTopology::TriangleList},
          {VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
           feme::graphics::PrimitiveTopology::TriangleStrip},
          {VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
           feme::graphics::PrimitiveTopology::TriangleFan},
      };
  for (auto [VkTopology, ExpectedTopology] : Cases) {
    VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
    InputAssembly.topology = VkTopology;
    VkPipeline Pipe = VK_NULL_HANDLE;
    ASSERT_EQ(create(Info, Pipe), VK_SUCCESS) << "topology " << VkTopology;
    ASSERT_NE(Pipe, VK_NULL_HANDLE);

    auto *Graphics =
        static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
    DynamicGraphicsState Dynamic;
    feme::graphics::GraphicsPipeline Executor =
        Graphics->buildExecutorPipeline(Dynamic);
    EXPECT_EQ(Executor.getTopology(), ExpectedTopology)
        << "topology " << VkTopology;

    vkDestroyPipeline(Device, Pipe, nullptr);
  }

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

TEST_F(GraphicsPipelineTest, RejectsUnimplementedStateCombinations) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  VkPipeline Pipe = VK_NULL_HANDLE;

  // Rasterizer discard.
  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Raster.rasterizerDiscardEnable = VK_TRUE;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);

  // A dynamic state with no implemented path (`rasterizerDiscardEnable`
  // itself is unimplemented statically -- see the rasterizer-discard case
  // above -- so its `VK_EXT_extended_dynamic_state2` dynamic counterpart
  // has nowhere to go either; `VK_DYNAMIC_STATE_DEPTH_BIAS`/`_BOUNDS` are
  // both implemented now, roadmap H7d).
  Info = makeCreateInfo(Vertex, Fragment);
  VkDynamicState Unsupported = VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE;
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 1;
  DynamicInfo.pDynamicStates = &Unsupported;
  Info.pDynamicState = &DynamicInfo;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);

  // Depth testing with no depth attachment in the render target.
  Info = makeCreateInfo(Vertex, Fragment);
  VkPipelineDepthStencilStateCreateInfo DepthInfo{};
  DepthInfo.depthTestEnable = VK_TRUE;
  DepthInfo.depthCompareOp = VK_COMPARE_OP_LESS;
  Info.pDepthStencilState = &DepthInfo;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);

  // A stage this milestone does not compile.
  Info = makeCreateInfo(Vertex, Fragment);
  Stages[1].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);

  // Primitive restart with a list topology: only strip topologies restart.
  Info = makeCreateInfo(Vertex, Fragment);
  InputAssembly.primitiveRestartEnable = VK_TRUE;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

// Roadmap H3: `maxViewports` (`MaxViewportCount`, 16) is the real limit now;
// a pipeline declaring an in-range viewport/scissor count > 1 must succeed
// and the executor pipeline must carry that many viewport/scissor entries.
TEST_F(GraphicsPipelineTest, AcceptsMultipleViewportsAndScissors) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  VkPipeline Pipe = VK_NULL_HANDLE;

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  std::array<VkViewport, 4> Viewports = {{
      {0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f},
      {4.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f},
      {0.0f, 4.0f, 4.0f, 4.0f, 0.0f, 1.0f},
      {4.0f, 4.0f, 4.0f, 4.0f, 0.0f, 1.0f},
  }};
  std::array<VkRect2D, 4> Scissors = {{
      {{0, 0}, {4, 4}},
      {{4, 0}, {4, 4}},
      {{0, 4}, {4, 4}},
      {{4, 4}, {4, 4}},
  }};
  ViewportState.viewportCount = static_cast<uint32_t>(Viewports.size());
  ViewportState.pViewports = Viewports.data();
  ViewportState.scissorCount = static_cast<uint32_t>(Scissors.size());
  ViewportState.pScissors = Scissors.data();

  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  DynamicGraphicsState Dynamic;
  EXPECT_EQ(Graphics->resolveViewport(Dynamic).size(), Viewports.size());
  EXPECT_EQ(Graphics->resolveScissor(Dynamic).size(), Scissors.size());

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

// Roadmap H3: a viewport/scissor count beyond `maxViewports`
// (`MaxViewportCount`, 16) must still be rejected at pipeline creation.
TEST_F(GraphicsPipelineTest, RejectsTooManyViewports) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  VkPipeline Pipe = VK_NULL_HANDLE;

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  std::vector<VkViewport> Viewports(
      MaxViewportCount + 1, VkViewport{0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f});
  std::vector<VkRect2D> Scissors(MaxViewportCount + 1,
                                 VkRect2D{{0, 0}, {4, 4}});
  ViewportState.viewportCount = static_cast<uint32_t>(Viewports.size());
  ViewportState.pViewports = Viewports.data();
  ViewportState.scissorCount = static_cast<uint32_t>(Scissors.size());
  ViewportState.pScissors = Scissors.data();

  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Pipe, VK_NULL_HANDLE);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

TEST_F(GraphicsPipelineTest, AcceptsPrimitiveRestartOnTriangleStrip) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  InputAssembly.primitiveRestartEnable = VK_TRUE;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  DynamicGraphicsState Dynamic;
  feme::graphics::GraphicsPipeline Executor =
      Graphics->buildExecutorPipeline(Dynamic);
  EXPECT_TRUE(Executor.getPrimitiveRestartEnable());

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// `VK_CULL_MODE_FRONT_AND_BACK` is a legal `VkCullModeFlags` value (it
/// culls every primitive of the pipeline's topology, "no representation
/// for it" no longer describes this executor -- see
/// `feme::graphics::CullMode::FrontAndBack`), so pipeline creation must
/// accept it rather than fail.
TEST_F(GraphicsPipelineTest, AcceptsFrontAndBackCulling) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Raster.cullMode = VK_CULL_MODE_FRONT_AND_BACK;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  EXPECT_EQ(Graphics->buildExecutorPipeline(DynamicGraphicsState{})
                .getRasterState()
                .Cull,
            feme::graphics::CullMode::FrontAndBack);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap C4c) `VK_DYNAMIC_STATE_CULL_MODE`/`VK_DYNAMIC_STATE_FRONT_FACE`:
/// a pipeline may now declare either dynamic, and `buildExecutorPipeline`
/// must then read the per-draw snapshot rather than this pipeline's own
/// (here, deliberately mismatched) creation-time value.
TEST_F(GraphicsPipelineTest, DynamicCullModeAndFrontFaceOverrideStaticState) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Raster.cullMode = VK_CULL_MODE_BACK_BIT;
  Raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
  VkDynamicState DynStates[2] = {VK_DYNAMIC_STATE_CULL_MODE,
                                 VK_DYNAMIC_STATE_FRONT_FACE};
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 2;
  DynamicInfo.pDynamicStates = DynStates;
  Info.pDynamicState = &DynamicInfo;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  DynamicGraphicsState Dynamic;
  Dynamic.Cull = feme::graphics::CullMode::FrontAndBack;
  Dynamic.Front = feme::graphics::FrontFace::CounterClockwise;
  feme::graphics::RasterState Resolved =
      Graphics->buildExecutorPipeline(Dynamic).getRasterState();
  EXPECT_EQ(Resolved.Cull, feme::graphics::CullMode::FrontAndBack);
  EXPECT_EQ(Resolved.Front, feme::graphics::FrontFace::CounterClockwise);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap F5) `VkPipelineRasterizationLineStateCreateInfoKHR`, chained
/// from `pRasterizationState->pNext`, sets the line style/width/stipple
/// state `RasterState` now carries.
TEST_F(GraphicsPipelineTest, TranslatesLineRasterizationState) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Raster.lineWidth = 3.0f;
  VkPipelineRasterizationLineStateCreateInfoKHR LineState{};
  LineState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_KHR;
  LineState.lineRasterizationMode = VK_LINE_RASTERIZATION_MODE_BRESENHAM_KHR;
  LineState.stippledLineEnable = VK_TRUE;
  LineState.lineStippleFactor = 3;
  LineState.lineStipplePattern = 0x00FF;
  Raster.pNext = &LineState;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  feme::graphics::RasterState Resolved =
      Graphics->buildExecutorPipeline(DynamicGraphicsState{}).getRasterState();
  EXPECT_EQ(Resolved.LineMode,
            feme::graphics::LineRasterizationMode::Bresenham);
  EXPECT_EQ(Resolved.LineWidth, 3.0f);
  EXPECT_TRUE(Resolved.StippledLineEnable);
  EXPECT_EQ(Resolved.StippleFactor, 3u);
  EXPECT_EQ(Resolved.StipplePattern, 0x00FFu);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap H7c) `fillModeNonSolid`: `VK_POLYGON_MODE_LINE`/`_POINT` are
/// now accepted and translated to `RasterState::Polygon`, instead of
/// unconditionally rejecting any non-`FILL` mode.
TEST_F(GraphicsPipelineTest, TranslatesNonFillPolygonModes) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Raster.polygonMode = VK_POLYGON_MODE_LINE;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);
  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  EXPECT_EQ(Graphics->buildExecutorPipeline(DynamicGraphicsState{})
                .getRasterState()
                .Polygon,
            feme::graphics::PolygonMode::Line);
  vkDestroyPipeline(Device, Pipe, nullptr);

  Info = makeCreateInfo(Vertex, Fragment);
  Raster.polygonMode = VK_POLYGON_MODE_POINT;
  Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);
  Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  EXPECT_EQ(Graphics->buildExecutorPipeline(DynamicGraphicsState{})
                .getRasterState()
                .Polygon,
            feme::graphics::PolygonMode::Point);
  vkDestroyPipeline(Device, Pipe, nullptr);

  // An unrecognized `VkPolygonMode` (`VK_NV_fill_rectangle`'s own value,
  // never advertised) is still rejected.
  Info = makeCreateInfo(Vertex, Fragment);
  Raster.polygonMode = VK_POLYGON_MODE_FILL_RECTANGLE_NV;
  Pipe = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Pipe, VK_NULL_HANDLE);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap H7d) `depthClamp`/`depthBiasClamp`: a pipeline may enable
/// `depthClampEnable`/`depthBiasEnable` and set static bias factors, which
/// `translateRasterState` now accepts (instead of unconditionally
/// rejecting `rasterizerDiscardEnable`, `depthClampEnable`, and
/// `depthBiasEnable` together) and stores on `RasterState`.
TEST_F(GraphicsPipelineTest, TranslatesDepthClampAndBiasState) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Raster.depthClampEnable = VK_TRUE;
  Raster.depthBiasEnable = VK_TRUE;
  Raster.depthBiasConstantFactor = 2.0f;
  Raster.depthBiasClamp = 0.5f;
  Raster.depthBiasSlopeFactor = 1.5f;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  feme::graphics::RasterState Resolved =
      Graphics->buildExecutorPipeline(DynamicGraphicsState{}).getRasterState();
  EXPECT_TRUE(Resolved.DepthClampEnable);
  EXPECT_TRUE(Resolved.DepthBiasEnable);
  EXPECT_EQ(Resolved.DepthBiasConstantFactor, 2.0f);
  EXPECT_EQ(Resolved.DepthBiasClamp, 0.5f);
  EXPECT_EQ(Resolved.DepthBiasSlopeFactor, 1.5f);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap H7f/H7n) `sampleShadingEnable`/`alphaToOneEnable`/
/// `alphaToCoverageEnable` translate into
/// `GraphicsPipeline::getSampleShadingEnable()`/`getAlphaToOneEnable()`/
/// `getAlphaToCoverageEnable()` -- all three now real, unlike the
/// original roadmap H7f text's now-superseded claim that
/// `alphaToCoverageEnable` stayed rejected.
TEST_F(GraphicsPipelineTest, TranslatesSampleShadingAndAlphaToOneState) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Multisample.sampleShadingEnable = VK_TRUE;
  Multisample.minSampleShading = 1.0f;
  Multisample.alphaToOneEnable = VK_TRUE;
  Multisample.alphaToCoverageEnable = VK_TRUE;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  feme::graphics::GraphicsPipeline Executor =
      Graphics->buildExecutorPipeline(DynamicGraphicsState{});
  EXPECT_TRUE(Executor.getSampleShadingEnable());
  EXPECT_TRUE(Executor.getAlphaToOneEnable());
  EXPECT_TRUE(Executor.getAlphaToCoverageEnable());

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap H7n) `alphaToCoverageEnable` alone (without the other two
/// `VkPipelineMultisampleStateCreateInfo` fields above) also translates
/// cleanly, and defaults to `false` when left unset.
TEST_F(GraphicsPipelineTest, TranslatesAlphaToCoverageState) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Multisample.alphaToCoverageEnable = VK_TRUE;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  feme::graphics::GraphicsPipeline Executor =
      Graphics->buildExecutorPipeline(DynamicGraphicsState{});
  EXPECT_TRUE(Executor.getAlphaToCoverageEnable());
  EXPECT_FALSE(Executor.getSampleShadingEnable());
  EXPECT_FALSE(Executor.getAlphaToOneEnable());

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (`DynamicLineWidthAndStippleOverrideStaticState`), a pipeline may
/// declare this dynamic and `buildExecutorPipeline` then reads the
/// per-draw snapshot instead of its own (deliberately mismatched)
/// creation-time bias factors.
TEST_F(GraphicsPipelineTest, DynamicDepthBiasOverridesStaticState) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Raster.depthBiasEnable = VK_TRUE;
  Raster.depthBiasConstantFactor = 1.0f;
  Raster.depthBiasClamp = 1.0f;
  Raster.depthBiasSlopeFactor = 1.0f;
  VkDynamicState Dynamic = VK_DYNAMIC_STATE_DEPTH_BIAS;
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 1;
  DynamicInfo.pDynamicStates = &Dynamic;
  Info.pDynamicState = &DynamicInfo;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  DynamicGraphicsState DynState;
  DynState.DepthBiasConstantFactor = 4.0f;
  DynState.DepthBiasClamp = 3.0f;
  DynState.DepthBiasSlopeFactor = 2.0f;
  feme::graphics::RasterState Resolved =
      Graphics->buildExecutorPipeline(DynState).getRasterState();
  EXPECT_EQ(Resolved.DepthBiasConstantFactor, 4.0f);
  EXPECT_EQ(Resolved.DepthBiasClamp, 3.0f);
  EXPECT_EQ(Resolved.DepthBiasSlopeFactor, 2.0f);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap H7d) `depthBounds`: a pipeline may enable
/// `depthBoundsTestEnable` with static `min`/`maxDepthBounds`, which
/// `translateDepthStencilState` now accepts (instead of unconditionally
/// rejecting it) and stores on `DepthState` -- needs a depth attachment in
/// its render target, exactly like the regular depth test.
TEST_F(GraphicsPipelineTest, TranslatesDepthBoundsState) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Info.renderPass = PassWithDepth;
  VkPipelineDepthStencilStateCreateInfo DepthInfo{};
  DepthInfo.depthBoundsTestEnable = VK_TRUE;
  DepthInfo.minDepthBounds = 0.25f;
  DepthInfo.maxDepthBounds = 0.75f;
  Info.pDepthStencilState = &DepthInfo;
  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  feme::graphics::DepthState Resolved =
      Graphics->buildExecutorPipeline(DynamicGraphicsState{}).getDepthState();
  EXPECT_TRUE(Resolved.BoundsTestEnable);
  EXPECT_EQ(Resolved.MinDepthBounds, 0.25f);
  EXPECT_EQ(Resolved.MaxDepthBounds, 0.75f);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap H7d) `VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE`/`_BOUNDS`:
/// like the dynamic depth-test-enable/compare-op states above, a pipeline
/// may declare either or both dynamic and `buildExecutorPipeline` then
/// resolves from the per-draw snapshot instead of the (deliberately
/// mismatched) static state.
TEST_F(GraphicsPipelineTest, DynamicDepthBoundsOverridesStaticState) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Info.renderPass = PassWithDepth;
  VkPipelineDepthStencilStateCreateInfo DepthInfo{};
  DepthInfo.depthBoundsTestEnable = VK_FALSE;
  DepthInfo.minDepthBounds = 0.0f;
  DepthInfo.maxDepthBounds = 1.0f;
  Info.pDepthStencilState = &DepthInfo;
  VkDynamicState DynStates[2] = {VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE,
                                 VK_DYNAMIC_STATE_DEPTH_BOUNDS};
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 2;
  DynamicInfo.pDynamicStates = DynStates;
  Info.pDynamicState = &DynamicInfo;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  DynamicGraphicsState DynState;
  DynState.DepthBoundsTestEnable = true;
  DynState.MinDepthBounds = 0.1f;
  DynState.MaxDepthBounds = 0.9f;
  feme::graphics::DepthState Resolved =
      Graphics->buildExecutorPipeline(DynState).getDepthState();
  EXPECT_TRUE(Resolved.BoundsTestEnable);
  EXPECT_EQ(Resolved.MinDepthBounds, 0.1f);
  EXPECT_EQ(Resolved.MaxDepthBounds, 0.9f);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap F6) `VkPipelineVertexInputDivisorStateCreateInfo` overrides a
/// per-instance binding's default divisor (1) with an explicit value,
/// recorded on the pipeline's own `VertexInputBinding` for the executor's
/// fetch-index formula to use.
TEST_F(GraphicsPipelineTest, TranslatesVertexAttributeDivisorState) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkVertexInputBindingDescription BindingDesc{0, 16,
                                              VK_VERTEX_INPUT_RATE_INSTANCE};
  VertexInput.vertexBindingDescriptionCount = 1;
  VertexInput.pVertexBindingDescriptions = &BindingDesc;
  VkVertexInputBindingDivisorDescription DivisorDesc{/*binding=*/0,
                                                     /*divisor=*/3};
  VkPipelineVertexInputDivisorStateCreateInfo DivisorState{};
  DivisorState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO;
  DivisorState.vertexBindingDivisorCount = 1;
  DivisorState.pVertexBindingDivisors = &DivisorDesc;
  VertexInput.pNext = &DivisorState;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  ASSERT_EQ(Graphics->vertexBindings().size(), 1u);
  EXPECT_EQ(Graphics->vertexBindings()[0].Divisor, 3u);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap F6) A divisor of `0` (`vertexAttributeInstanceRateZeroDivisor`)
/// is accepted too: it is not a new mechanism, just this same per-binding
/// field's own degenerate value.
TEST_F(GraphicsPipelineTest, AcceptsZeroVertexAttributeDivisor) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkVertexInputBindingDescription BindingDesc{0, 16,
                                              VK_VERTEX_INPUT_RATE_INSTANCE};
  VertexInput.vertexBindingDescriptionCount = 1;
  VertexInput.pVertexBindingDescriptions = &BindingDesc;
  VkVertexInputBindingDivisorDescription DivisorDesc{/*binding=*/0,
                                                     /*divisor=*/0};
  VkPipelineVertexInputDivisorStateCreateInfo DivisorState{};
  DivisorState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO;
  DivisorState.vertexBindingDivisorCount = 1;
  DivisorState.pVertexBindingDivisors = &DivisorDesc;
  VertexInput.pNext = &DivisorState;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  ASSERT_EQ(Graphics->vertexBindings().size(), 1u);
  EXPECT_EQ(Graphics->vertexBindings()[0].Divisor, 0u);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap F6) A `VkVertexInputBindingDivisorDescription` naming a binding
/// the pipeline never declared, or one declared
/// `VK_VERTEX_INPUT_RATE_VERTEX` (the divisor only ever applies to a
/// per-instance binding), or a divisor exceeding `maxVertexAttribDivisor`,
/// is rejected at creation rather than silently ignored or clamped.
TEST_F(GraphicsPipelineTest, RejectsInvalidVertexAttributeDivisorState) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  VkPipeline Pipe = VK_NULL_HANDLE;

  // Names a binding the pipeline does not declare.
  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkVertexInputBindingDescription BindingDesc{0, 16,
                                              VK_VERTEX_INPUT_RATE_INSTANCE};
  VertexInput.vertexBindingDescriptionCount = 1;
  VertexInput.pVertexBindingDescriptions = &BindingDesc;
  VkVertexInputBindingDivisorDescription BadBinding{/*binding=*/1,
                                                    /*divisor=*/2};
  VkPipelineVertexInputDivisorStateCreateInfo DivisorState{};
  DivisorState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO;
  DivisorState.vertexBindingDivisorCount = 1;
  DivisorState.pVertexBindingDivisors = &BadBinding;
  VertexInput.pNext = &DivisorState;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);

  // Names a `VK_VERTEX_INPUT_RATE_VERTEX` binding.
  Info = makeCreateInfo(Vertex, Fragment);
  VkVertexInputBindingDescription VertexRateBinding{
      0, 16, VK_VERTEX_INPUT_RATE_VERTEX};
  VertexInput.vertexBindingDescriptionCount = 1;
  VertexInput.pVertexBindingDescriptions = &VertexRateBinding;
  VkVertexInputBindingDivisorDescription WrongRate{/*binding=*/0,
                                                   /*divisor=*/2};
  DivisorState.pVertexBindingDivisors = &WrongRate;
  VertexInput.pNext = &DivisorState;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// STIPPLE_KHR`: a pipeline may declare either dynamic, and
/// `buildExecutorPipeline` must then read the per-draw snapshot rather
/// than this pipeline's own (deliberately mismatched) creation-time
/// value.
TEST_F(GraphicsPipelineTest, DynamicLineWidthAndStippleOverrideStaticState) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Raster.lineWidth = 1.0f;
  VkPipelineRasterizationLineStateCreateInfoKHR LineState{};
  LineState.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_KHR;
  LineState.lineRasterizationMode = VK_LINE_RASTERIZATION_MODE_RECTANGULAR_KHR;
  LineState.stippledLineEnable = VK_TRUE;
  LineState.lineStippleFactor = 1;
  LineState.lineStipplePattern = 0x0001;
  Raster.pNext = &LineState;
  VkDynamicState DynStates[2] = {VK_DYNAMIC_STATE_LINE_WIDTH,
                                 VK_DYNAMIC_STATE_LINE_STIPPLE_KHR};
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 2;
  DynamicInfo.pDynamicStates = DynStates;
  Info.pDynamicState = &DynamicInfo;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  DynamicGraphicsState Dynamic;
  Dynamic.LineWidth = 5.0f;
  Dynamic.StippleFactor = 7;
  Dynamic.StipplePattern = 0xABCD;
  feme::graphics::RasterState Resolved =
      Graphics->buildExecutorPipeline(Dynamic).getRasterState();
  EXPECT_EQ(Resolved.LineWidth, 5.0f);
  EXPECT_EQ(Resolved.StippleFactor, 7u);
  EXPECT_EQ(Resolved.StipplePattern, 0xABCDu);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap C4c) A pipeline declaring `VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE`
/// (or `_WRITE_ENABLE`) dynamic may still enable the test at draw time even
/// though its own static `depthTestEnable`/`depthWriteEnable` are both
/// `VK_FALSE` -- so it still needs a depth attachment in its render target,
/// exactly like a pipeline whose *static* fields already enable the test.
TEST_F(GraphicsPipelineTest, DynamicDepthTestEnableRequiresDepthAttachment) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkDynamicState Dynamic = VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE;
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 1;
  DynamicInfo.pDynamicStates = &Dynamic;
  Info.pDynamicState = &DynamicInfo;
  // `Info.renderPass` (set by `makeCreateInfo`) is the fixture's
  // depth-less `Pass`.

  VkPipeline Pipe = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Pipe, VK_NULL_HANDLE);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// The same three dynamic states, this time over `PassWithDepth`: creation
/// succeeds despite a static depth-stencil state with the test disabled
/// (and, deliberately, `depthCompareOp` left at its zero-initialized
/// `VK_COMPARE_OP_NEVER`, which `translateDepthStencilState` never even
/// looks at while `DynamicStateDepthCompareOp` is set), and
/// `buildExecutorPipeline` resolves depth state from the per-draw snapshot.
TEST_F(GraphicsPipelineTest, DynamicDepthStateOverridesStaticState) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Info.renderPass = PassWithDepth;
  VkPipelineDepthStencilStateCreateInfo DepthInfo{};
  Info.pDepthStencilState = &DepthInfo;
  VkDynamicState DynStates[3] = {VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
                                 VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
                                 VK_DYNAMIC_STATE_DEPTH_COMPARE_OP};
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 3;
  DynamicInfo.pDynamicStates = DynStates;
  Info.pDynamicState = &DynamicInfo;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  DynamicGraphicsState Dynamic;
  Dynamic.DepthTestEnable = true;
  Dynamic.DepthWriteEnable = true;
  Dynamic.DepthCompare = feme::graphics::CompareOp::Greater;
  feme::graphics::DepthState Resolved =
      Graphics->buildExecutorPipeline(Dynamic).getDepthState();
  EXPECT_TRUE(Resolved.TestEnable);
  EXPECT_TRUE(Resolved.WriteEnable);
  EXPECT_EQ(Resolved.Compare, feme::graphics::CompareOp::Greater);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap C4c) `VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE`/`_STENCIL_OP`: a
/// depth-less-but-stencil-attached render target is enough (stencil
/// testing needs its own `S8_UINT` attachment, not a depth one), and a
/// pipeline created with stencil testing statically disabled still
/// resolves the dynamically-enabled test and its ops.
TEST_F(GraphicsPipelineTest, DynamicStencilStateOverridesStaticState) {
  // A render pass with an `S8_UINT`-only depth-stencil attachment (see
  // `isSupportedStencilAttachmentFormat`), distinct from the fixture's own
  // `PassWithDepth` (`D32_SFLOAT`, which has no stencil aspect at all).
  VkAttachmentDescription Attachment{};
  Attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
  Attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  Attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  Attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentDescription StencilAttachment{};
  StencilAttachment.format = VK_FORMAT_S8_UINT;
  StencilAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  StencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  StencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentDescription Attachments[2] = {Attachment, StencilAttachment};
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
  VkRenderPass PassWithStencil = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreateRenderPass(Device, &PassInfo, nullptr, &PassWithStencil),
            VK_SUCCESS);

  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Info.renderPass = PassWithStencil;
  VkPipelineDepthStencilStateCreateInfo DepthInfo{};
  Info.pDepthStencilState = &DepthInfo;
  VkDynamicState DynStates[2] = {VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
                                 VK_DYNAMIC_STATE_STENCIL_OP};
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 2;
  DynamicInfo.pDynamicStates = DynStates;
  Info.pDynamicState = &DynamicInfo;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  DynamicGraphicsState Dynamic;
  Dynamic.StencilTestEnable = true;
  Dynamic.StencilOps[0].FailOp = feme::graphics::StencilOp::Replace;
  Dynamic.StencilOps[0].PassOp = feme::graphics::StencilOp::IncrementClamp;
  Dynamic.StencilOps[0].DepthFailOp = feme::graphics::StencilOp::Zero;
  Dynamic.StencilOps[0].Compare = feme::graphics::CompareOp::Equal;
  feme::graphics::StencilState Resolved =
      Graphics->buildExecutorPipeline(Dynamic).getStencilState();
  EXPECT_TRUE(Resolved.TestEnable);
  EXPECT_EQ(Resolved.Front.FailOp, feme::graphics::StencilOp::Replace);
  EXPECT_EQ(Resolved.Front.PassOp, feme::graphics::StencilOp::IncrementClamp);
  EXPECT_EQ(Resolved.Front.DepthFailOp, feme::graphics::StencilOp::Zero);
  EXPECT_EQ(Resolved.Front.Compare, feme::graphics::CompareOp::Equal);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyRenderPass(Device, PassWithStencil, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap C4c) `VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT`/`_SCISSOR_WITH_
/// COUNT`: the same effective dynamic state as `VIEWPORT`/`SCISSOR`, so a
/// pipeline may declare `viewportCount`/`scissorCount` as anything (even
/// `0`, as here) once either "with count" state makes that field ignored,
/// and `resolveViewport`/`resolveScissor` still read the per-draw snapshot.
TEST_F(GraphicsPipelineTest, ViewportWithCountIsTheSameDynamicStateAsViewport) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  ViewportState.viewportCount = 0;
  ViewportState.pViewports = nullptr;
  ViewportState.scissorCount = 0;
  ViewportState.pScissors = nullptr;
  VkDynamicState DynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
                                 VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT};
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 2;
  DynamicInfo.pDynamicStates = DynStates;
  Info.pDynamicState = &DynamicInfo;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  DynamicGraphicsState Dynamic;
  Dynamic.Viewports[0] =
      feme::graphics::ViewportState{1.0f, 2.0f, 8.0f, 8.0f, 0.0f, 1.0f};
  Dynamic.Scissors[0] = feme::graphics::ScissorRect{0, 0, 8, 8};
  EXPECT_EQ(Graphics->resolveViewport(Dynamic)[0].Width, 8.0f);
  EXPECT_EQ(Graphics->resolveScissor(Dynamic)[0].Width, 8u);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap C4c) `VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY`, restricted to the
/// triangle class this executor implements: a pipeline created with
/// `TriangleList` may resolve to `TriangleStrip` at draw time (still the
/// same class), and an out-of-class dynamic value (a defensive case no
/// conformant caller reaches, per `DynamicGraphicsState::Topology`'s own
/// comment) falls back to the pipeline's own static topology rather than
/// resolving to something unspecified.
TEST_F(GraphicsPipelineTest,
       DynamicPrimitiveTopologySwitchesWithinTriangleClass) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkDynamicState Dynamic = VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY;
  VkPipelineDynamicStateCreateInfo DynamicInfo{};
  DynamicInfo.dynamicStateCount = 1;
  DynamicInfo.pDynamicStates = &Dynamic;
  Info.pDynamicState = &DynamicInfo;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  ASSERT_NE(Pipe, VK_NULL_HANDLE);

  auto *Graphics = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  DynamicGraphicsState Dyn;
  Dyn.Topology = feme::graphics::PrimitiveTopology::TriangleStrip;
  EXPECT_EQ(Graphics->buildExecutorPipeline(Dyn).getTopology(),
            feme::graphics::PrimitiveTopology::TriangleStrip);

  // The defensive fallback: an unmapped dynamic value (`nullopt`) resolves
  // to the pipeline's own static topology (`TriangleList`, per
  // `makeCreateInfo`) instead.
  DynamicGraphicsState Fallback;
  EXPECT_EQ(Graphics->buildExecutorPipeline(Fallback).getTopology(),
            feme::graphics::PrimitiveTopology::TriangleList);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// A fragment stage writing no `SV_Target0` cannot fill the render pass's
/// one color attachment; the mismatch is a creation failure, not a draw-time
/// surprise.
TEST_F(GraphicsPipelineTest, RejectsMissingFragmentOutput) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @main() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir");
  ASSERT_NE(Fragment, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkPipeline Pipe = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap H7t) A fragment stage's `SV_Target0` output may legally have
/// fewer than 4 components (e.g. a `vec3`, discovered via a real
/// `dEQP-VK.pipeline.monolithic.multisample.alpha_to_coverage_unused_attachment.*`
/// re-run whose real attachment writes exactly this shape) -- the missing
/// trailing components are simply never written by the shader and read
/// back as their SPIR-V/GLSL-defined identity value at draw time
/// (`Executor.cpp`'s `readFragmentColor`), not a creation-time error.
TEST_F(GraphicsPipelineTest, AcceptsFragmentOutputNarrowerThan4Components) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(R"mlir(
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
)mlir");
  ASSERT_NE(Fragment, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkPipeline Pipe = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Pipe), VK_SUCCESS);
  EXPECT_NE(Pipe, VK_NULL_HANDLE);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (Roadmap H11) A fragment stage need not declare an output at every
/// location a pipeline's own (real, non-`VK_ATTACHMENT_UNUSED`) color
/// attachment format occupies -- simply never writing a location is legal
/// per the Vulkan spec's fragment-output-interface rules (the attachment
/// keeps whatever it already held), not a pipeline-creation-time error.
/// CTS's own `dEQP-VK.renderpasses.*.unused_clear_attachments.*` family (and
/// several `sampleread`/`load_store_op_none` siblings) shares one pipeline
/// across many draws, each binding a different subset of that pipeline's
/// own declared attachments, with a fragment stage that only ever declares
/// an output for the locations *some* draw plans to use.
TEST_F(GraphicsPipelineTest,
      AcceptsFragmentStageNotWritingEveryColorAttachmentLocation) {
  VkShaderModule Vertex = createModule(VertexSource);
  // Declares only a location-0 output; a real (`R8G8B8A8_UNORM`) location-1
  // color attachment below has no matching fragment output at all.
  VkShaderModule Fragment = createModule(FragmentSource);
  ASSERT_NE(Fragment, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  std::array<VkFormat, 2> ColorFormats{VK_FORMAT_R8G8B8A8_UNORM,
                                       VK_FORMAT_R8G8B8A8_UNORM};
  std::array<VkPipelineColorBlendAttachmentState, 2> BlendAttachments{};
  BlendAttachments[0].colorWriteMask = 0xF;
  BlendAttachments[1].colorWriteMask = 0xF;
  Blend.attachmentCount = 2;
  Blend.pAttachments = BlendAttachments.data();
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 2;
  Rendering.pColorAttachmentFormats = ColorFormats.data();
  Info.renderPass = VK_NULL_HANDLE;
  Info.pNext = &Rendering;

  VkPipeline Pipe = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Pipe), VK_SUCCESS);
  EXPECT_NE(Pipe, VK_NULL_HANDLE);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// A fragment input at a location no vertex output writes is a mislinked
/// varying; cross-stage interface matching catches it at creation.
TEST_F(GraphicsPipelineTest, RejectsUnmatchedVarying) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @in_var {location = 3 : i32} : !spirv.ptr<vector<4xf32>, Input>
  spirv.GlobalVariable @color {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %ip = spirv.mlir.addressof @in_var : !spirv.ptr<vector<4xf32>, Input>
    %v = spirv.Load "Input" %ip : vector<4xf32>
    %p = spirv.mlir.addressof @color : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %p, %v : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main, @in_var, @color
  spirv.ExecutionMode @main "OriginUpperLeft"
}
)mlir");
  ASSERT_NE(Fragment, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkPipeline Pipe = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// A dynamic-rendering pipeline names its attachment formats through a
/// chained `VkPipelineRenderingCreateInfo` instead of a `VkRenderPass`, and
/// normalizes into exactly the same translated state.
TEST_F(GraphicsPipelineTest, AcceptsDynamicRenderingFormats) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkFormat ColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 1;
  Rendering.pColorAttachmentFormats = &ColorFormat;
  Info.renderPass = VK_NULL_HANDLE;
  Info.pNext = &Rendering;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  EXPECT_EQ(static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe))
                ->colorAttachmentCount(),
            1u);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H2b: a depth-only pipeline -- zero color attachments and a
/// fragment stage with no color output -- is legal Vulkan
/// (`dEQP-VK.multiview.depth_without_fragment_shader`'s own shape) and must
/// build rather than being rejected for lacking a color attachment.
TEST_F(GraphicsPipelineTest, AcceptsZeroColorAttachments) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(NoColorOutputFragmentSource);
  ASSERT_NE(Vertex, VK_NULL_HANDLE);
  ASSERT_NE(Fragment, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Blend.attachmentCount = 0;
  Blend.pAttachments = nullptr;
  VkPipelineDepthStencilStateCreateInfo DepthInfo{};
  DepthInfo.depthTestEnable = VK_TRUE;
  DepthInfo.depthWriteEnable = VK_TRUE;
  DepthInfo.depthCompareOp = VK_COMPARE_OP_LESS;
  Info.pDepthStencilState = &DepthInfo;

  VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 0;
  Rendering.depthAttachmentFormat = DepthFormat;
  Info.renderPass = VK_NULL_HANDLE;
  Info.pNext = &Rendering;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  EXPECT_EQ(static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe))
                ->colorAttachmentCount(),
            0u);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H7n: a `VkRenderPass` subpass with zero color attachments (a
/// real depth/stencil-only render, e.g. `dEQP-VK.pipeline.monolithic.
/// multisample.alpha_to_coverage_no_color_attachment.*`'s own
/// `RENDER_TYPE_DEPTHSTENCIL_ONLY`) must derive its render target sample
/// count from the depth/stencil attachment rather than silently staying at
/// the single-sample default (the only place that ever set it, the loop
/// over `Subpass.ColorAttachments`, never executes when that list is
/// empty) -- otherwise a genuinely multisampled depth/stencil-only
/// pipeline is wrongly rejected as "disagreeing" with a render target
/// whose real sample count this code never actually consulted.
TEST_F(GraphicsPipelineTest, AcceptsMultisampledZeroColorRenderPass) {
  VkAttachmentDescription DepthOnlyAttachment{};
  DepthOnlyAttachment.format = VK_FORMAT_D32_SFLOAT;
  DepthOnlyAttachment.samples = VK_SAMPLE_COUNT_4_BIT;
  DepthOnlyAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  DepthOnlyAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentReference DepthOnlyRef{
      0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription DepthOnlySubpass{};
  DepthOnlySubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  DepthOnlySubpass.colorAttachmentCount = 0;
  DepthOnlySubpass.pDepthStencilAttachment = &DepthOnlyRef;
  VkRenderPassCreateInfo DepthOnlyPassInfo{};
  DepthOnlyPassInfo.attachmentCount = 1;
  DepthOnlyPassInfo.pAttachments = &DepthOnlyAttachment;
  DepthOnlyPassInfo.subpassCount = 1;
  DepthOnlyPassInfo.pSubpasses = &DepthOnlySubpass;
  VkRenderPass DepthOnlyPass = VK_NULL_HANDLE;
  ASSERT_EQ(
      vkCreateRenderPass(Device, &DepthOnlyPassInfo, nullptr, &DepthOnlyPass),
      VK_SUCCESS);

  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(NoColorOutputFragmentSource);
  ASSERT_NE(Vertex, VK_NULL_HANDLE);
  ASSERT_NE(Fragment, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Info.renderPass = DepthOnlyPass;
  Blend.attachmentCount = 0;
  Blend.pAttachments = nullptr;
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;
  VkPipelineDepthStencilStateCreateInfo DepthInfo{};
  DepthInfo.depthTestEnable = VK_TRUE;
  DepthInfo.depthWriteEnable = VK_TRUE;
  DepthInfo.depthCompareOp = VK_COMPARE_OP_LESS;
  Info.pDepthStencilState = &DepthInfo;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  EXPECT_EQ(static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe))
                ->colorAttachmentCount(),
            0u);

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
  vkDestroyRenderPass(Device, DepthOnlyPass, nullptr);
}

/// Roadmap H2j: a depth-only pipeline may omit the fragment stage from
/// `pStages` entirely -- distinct from `AcceptsZeroColorAttachments` above
/// (whose fragment stage is present but merely writes no color output) --
/// exactly `dEQP-VK.multiview.depth_without_fragment_shader`'s own shape
/// (`VUID-VkGraphicsPipelineCreateInfo-pStages-06894`/neighbors).
TEST_F(GraphicsPipelineTest, AcceptsMissingFragmentStage) {
  VkShaderModule Vertex = createModule(VertexSource);
  ASSERT_NE(Vertex, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, VK_NULL_HANDLE);
  Info.stageCount = 1;
  Blend.attachmentCount = 0;
  Blend.pAttachments = nullptr;
  VkPipelineDepthStencilStateCreateInfo DepthInfo{};
  DepthInfo.depthTestEnable = VK_TRUE;
  DepthInfo.depthWriteEnable = VK_TRUE;
  DepthInfo.depthCompareOp = VK_COMPARE_OP_LESS;
  Info.pDepthStencilState = &DepthInfo;

  VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 0;
  Rendering.depthAttachmentFormat = DepthFormat;
  Info.renderPass = VK_NULL_HANDLE;
  Info.pNext = &Rendering;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  auto *Pipe2 = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  EXPECT_EQ(Pipe2->colorAttachmentCount(), 0u);
  EXPECT_FALSE(Pipe2->hasFragmentStage());

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H9a: a pipeline whose render target has one or more color
/// attachments may still legally omit its fragment stage -- per the
/// spec's own "Valid Combinations of Stages for Graphics Pipelines" text,
/// "If a fragment shader is omitted, fragment color outputs have
/// undefined values", with no condition on the color attachment count.
/// Exactly `dEQP-VK.query_pool.statistics_query.*`'s own
/// `VertexShaderTestInstance` `vertexOnlyPipe` shape, which declares one
/// (necessarily unwritten) color attachment via `pColorBlendState` while
/// omitting the fragment stage from `pStages` entirely. Originally
/// rejected outright (`VK_ERROR_INITIALIZATION_FAILED`), which was
/// stricter than the spec allows.
TEST_F(GraphicsPipelineTest, AcceptsMissingFragmentStageWithColorAttachments) {
  VkShaderModule Vertex = createModule(VertexSource);
  ASSERT_NE(Vertex, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, VK_NULL_HANDLE);
  Info.stageCount = 1;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  auto *Pipe2 = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  EXPECT_EQ(Pipe2->colorAttachmentCount(), 1u);
  EXPECT_FALSE(Pipe2->hasFragmentStage());

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H2j: when a pipeline has no fragment stage, it has no fragment
/// output interface, so per the Vulkan spec `pColorBlendState` -- including
/// its own `attachmentCount` -- must be entirely ignored rather than
/// validated against the render target's (necessarily empty) color
/// attachments. Exercises exactly the shape real CTS tests build (e.g.
/// `vktMultiViewRenderTests.cpp`'s `depth_without_fragment_shader` case),
/// which hardcodes `attachmentCount = 1` unconditionally even with zero
/// color attachments and no fragment shader.
TEST_F(GraphicsPipelineTest,
       AcceptsMissingFragmentStageWithMismatchedColorBlendState) {
  VkShaderModule Vertex = createModule(VertexSource);
  ASSERT_NE(Vertex, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, VK_NULL_HANDLE);
  Info.stageCount = 1;
  // Deliberately left at the fixture's default of 1, mismatching the zero
  // color attachments below -- this must be ignored, not rejected.
  ASSERT_EQ(Blend.attachmentCount, 1u);
  VkPipelineDepthStencilStateCreateInfo DepthInfo{};
  DepthInfo.depthTestEnable = VK_TRUE;
  DepthInfo.depthWriteEnable = VK_TRUE;
  DepthInfo.depthCompareOp = VK_COMPARE_OP_LESS;
  Info.pDepthStencilState = &DepthInfo;

  VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 0;
  Rendering.depthAttachmentFormat = DepthFormat;
  Info.renderPass = VK_NULL_HANDLE;
  Info.pNext = &Rendering;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  auto *Pipe2 = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  EXPECT_EQ(Pipe2->colorAttachmentCount(), 0u);
  EXPECT_FALSE(Pipe2->hasFragmentStage());

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H9a: `VUID-VkGraphicsPipelineCreateInfo-renderPass-07609` only
/// requires `pColorBlendState->attachmentCount` to match the render
/// target's own color attachment count when "the subpass uses color
/// attachments" -- unlike `AcceptsMissingFragmentStageWithMismatchedColor
/// BlendState` above, this pipeline has a real fragment stage (which
/// writes only `gl_FragDepth`, matching the render target's own lack of
/// any color attachment), confirming the same tolerance applies whether or
/// not a fragment stage is present, not only when one is missing entirely.
/// Exactly `dEQP-VK.query_pool.statistics_query.*`'s own
/// `GeometryShaderTestInstance`/`TessellationShaderTestInstance`-family
/// `noColorAttachments` shape, which keeps a real fragment stage while
/// still hardcoding `ColorBlendState(1, &attachmentState)` unconditionally.
TEST_F(GraphicsPipelineTest,
       AcceptsFragmentStageWithMismatchedColorBlendStateAndNoColorAttachments) {
  VkShaderModule Vertex = createModule(VertexSource);
  ASSERT_NE(Vertex, VK_NULL_HANDLE);
  VkShaderModule Fragment = createModule(NoColorOutputFragmentSource);
  ASSERT_NE(Fragment, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  // Deliberately left at the fixture's default of 1, mismatching the zero
  // color attachments below -- this must be ignored, not rejected, exactly
  // like the fragment-less case above.
  ASSERT_EQ(Blend.attachmentCount, 1u);
  VkPipelineDepthStencilStateCreateInfo DepthInfo{};
  DepthInfo.depthTestEnable = VK_TRUE;
  DepthInfo.depthWriteEnable = VK_TRUE;
  DepthInfo.depthCompareOp = VK_COMPARE_OP_LESS;
  Info.pDepthStencilState = &DepthInfo;

  VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;
  VkPipelineRenderingCreateInfo Rendering{};
  Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  Rendering.colorAttachmentCount = 0;
  Rendering.depthAttachmentFormat = DepthFormat;
  Info.renderPass = VK_NULL_HANDLE;
  Info.pNext = &Rendering;

  VkPipeline Pipe = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Pipe), VK_SUCCESS);
  auto *Pipe2 = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Pipe));
  EXPECT_EQ(Pipe2->colorAttachmentCount(), 0u);
  EXPECT_TRUE(Pipe2->hasFragmentStage());

  vkDestroyPipeline(Device, Pipe, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap C1 ("Mandatory formats"): every format
/// `isSupportedColorAttachmentFormat` grants Vulkan 1.2's mandatory
/// `COLOR_ATTACHMENT_BIT | COLOR_ATTACHMENT_BLEND_BIT` status to must build
/// a pipeline the same way `VK_FORMAT_R8G8B8A8_UNORM` already does. Roadmap
/// E5 extends this same acceptance to `VK_KHR_maintenance5`'s two new
/// formats, `A8_UNORM`/`A1B5G5R5_UNORM_PACK16`, which are not mandatory but
/// are `COLOR_ATTACHMENT_BIT | COLOR_ATTACHMENT_BLEND_BIT` capable.
TEST_F(GraphicsPipelineTest, AcceptsMandatoryColorAttachmentFormats) {
  for (VkFormat Format :
       {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_R16G16B16A16_SFLOAT,
        // Roadmap E5: `VK_KHR_maintenance5`'s two new formats.
        VK_FORMAT_A8_UNORM_KHR, VK_FORMAT_A1B5G5R5_UNORM_PACK16_KHR}) {
    VkShaderModule Vertex = createModule(VertexSource);
    VkShaderModule Fragment = createModule(FragmentSource);

    VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
    VkPipelineRenderingCreateInfo Rendering{};
    Rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    Rendering.colorAttachmentCount = 1;
    Rendering.pColorAttachmentFormats = &Format;
    Info.renderPass = VK_NULL_HANDLE;
    Info.pNext = &Rendering;

    VkPipeline Pipe = VK_NULL_HANDLE;
    EXPECT_EQ(create(Info, Pipe), VK_SUCCESS) << "format " << Format;
    vkDestroyPipeline(Device, Pipe, nullptr);
    vkDestroyShaderModule(Device, Fragment, nullptr);
    vkDestroyShaderModule(Device, Vertex, nullptr);
  }
}

/// An identical `VkGraphicsPipelineCreateInfo` (same SPIR-V, same layout,
/// same fixed-function state) creates a cache hit that shares the compiled
/// stages rather than recompiling them.
TEST_F(GraphicsPipelineTest, CachedPipelineSharesCompiledStages) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  VkPipelineCacheCreateInfo CacheInfo{};
  VkPipelineCache Cache = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineCache(Device, &CacheInfo, nullptr, &Cache),
            VK_SUCCESS);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkPipeline First = VK_NULL_HANDLE, Second = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, First, Cache), VK_SUCCESS);
  ASSERT_EQ(create(Info, Second, Cache), VK_SUCCESS);

  auto *FirstPipe =
      static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(First));
  auto *SecondPipe =
      static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Second));
  EXPECT_EQ(&FirstPipe->vertexStage(), &SecondPipe->vertexStage());
  EXPECT_EQ(&FirstPipe->fragmentStage(), &SecondPipe->fragmentStage());

  vkDestroyPipeline(Device, First, nullptr);
  vkDestroyPipeline(Device, Second, nullptr);
  vkDestroyPipelineCache(Device, Cache, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Without a `VkPipelineCache`, two otherwise-identical creations compile
/// independent artifacts.
TEST_F(GraphicsPipelineTest, NoCacheCompilesIndependentStagesEachTime) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkPipeline First = VK_NULL_HANDLE, Second = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, First), VK_SUCCESS);
  ASSERT_EQ(create(Info, Second), VK_SUCCESS);

  auto *FirstPipe =
      static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(First));
  auto *SecondPipe =
      static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Second));
  EXPECT_NE(&FirstPipe->vertexStage(), &SecondPipe->vertexStage());

  vkDestroyPipeline(Device, First, nullptr);
  vkDestroyPipeline(Device, Second, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Two pipelines built from the same SPIR-V but disagreeing fixed-function
/// state (here, cull mode) must not share a cache entry: the key covers the
/// whole normalized pipeline description, not only the two stages' bytes.
TEST_F(GraphicsPipelineTest, DifferingFixedFunctionStateIsACacheMiss) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  VkPipelineCacheCreateInfo CacheInfo{};
  VkPipelineCache Cache = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineCache(Device, &CacheInfo, nullptr, &Cache),
            VK_SUCCESS);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  VkPipeline First = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, First, Cache), VK_SUCCESS);

  Info = makeCreateInfo(Vertex, Fragment);
  Raster.cullMode = VK_CULL_MODE_BACK_BIT;
  VkPipeline Second = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Second, Cache), VK_SUCCESS);

  auto *FirstPipe =
      static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(First));
  auto *SecondPipe =
      static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Second));
  EXPECT_NE(&FirstPipe->vertexStage(), &SecondPipe->vertexStage());

  vkDestroyPipeline(Device, First, nullptr);
  vkDestroyPipeline(Device, Second, nullptr);
  vkDestroyPipelineCache(Device, Cache, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap E9: `VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT`
/// with no cache at all must always report `VK_PIPELINE_COMPILE_REQUIRED`
/// and leave the pipeline null, the same as the compute path (see
/// `PipelineCacheTest.FailOnCompileRequiredWithNoCacheAlwaysFails`).
TEST_F(GraphicsPipelineTest, FailOnCompileRequiredWithNoCacheAlwaysFails) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Info.flags = VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;

  VkPipeline Pipeline = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Pipeline), VK_PIPELINE_COMPILE_REQUIRED);
  EXPECT_EQ(Pipeline, VK_NULL_HANDLE);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap E9: with a cache, a first creation carrying the bit misses (the
/// cache starts empty) without populating it; an ordinary creation then
/// compiles and populates it; a third creation with the bit set again now
/// hits and succeeds, reusing the second creation's compiled stages.
TEST_F(GraphicsPipelineTest, FailOnCompileRequiredSucceedsOnceCachePopulated) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  VkPipelineCacheCreateInfo CacheInfo{};
  VkPipelineCache Cache = VK_NULL_HANDLE;
  ASSERT_EQ(vkCreatePipelineCache(Device, &CacheInfo, nullptr, &Cache),
            VK_SUCCESS);

  VkGraphicsPipelineCreateInfo NoCompileInfo = makeCreateInfo(Vertex, Fragment);
  NoCompileInfo.flags =
      VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;
  VkPipeline Missed = VK_NULL_HANDLE;
  EXPECT_EQ(create(NoCompileInfo, Missed, Cache), VK_PIPELINE_COMPILE_REQUIRED);
  EXPECT_EQ(Missed, VK_NULL_HANDLE);

  VkGraphicsPipelineCreateInfo NormalInfo = makeCreateInfo(Vertex, Fragment);
  VkPipeline Compiled = VK_NULL_HANDLE;
  ASSERT_EQ(create(NormalInfo, Compiled, Cache), VK_SUCCESS);

  VkGraphicsPipelineCreateInfo HitInfo = makeCreateInfo(Vertex, Fragment);
  HitInfo.flags = VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;
  VkPipeline Hit = VK_NULL_HANDLE;
  ASSERT_EQ(create(HitInfo, Hit, Cache), VK_SUCCESS);

  auto *CompiledPipe =
      static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Compiled));
  auto *HitPipe = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Hit));
  EXPECT_EQ(&CompiledPipe->vertexStage(), &HitPipe->vertexStage());

  vkDestroyPipeline(Device, Compiled, nullptr);
  vkDestroyPipeline(Device, Hit, nullptr);
  vkDestroyPipelineCache(Device, Cache, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap F10) `VkPipelineRobustnessCreateInfo` is resolved independently
/// per stage: the vertex stage's own chained struct is honored for the
/// vertex stage, the pipeline-level one is the fragment stage's fallback
/// (it names no struct of its own here), matching the extension's own
/// "scoped to all accesses emanating from the shader code of this shader
/// stage" spec text.
TEST_F(GraphicsPipelineTest, ResolvesPipelineRobustnessPerStage) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkPipelineRobustnessCreateInfo PipelineRobustnessInfo{};
  PipelineRobustnessInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_ROBUSTNESS_CREATE_INFO;
  PipelineRobustnessInfo.images =
      VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DISABLED;

  VkPipelineRobustnessCreateInfo VertexRobustnessInfo{};
  VertexRobustnessInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_ROBUSTNESS_CREATE_INFO;
  VertexRobustnessInfo.vertexInputs =
      VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2;

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Info.pNext = &PipelineRobustnessInfo;
  Stages[0].pNext = &VertexRobustnessInfo;

  VkPipeline Handle = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Handle), VK_SUCCESS);

  auto *Pipe = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Handle));
  EXPECT_EQ(Pipe->vertexRobustness().VertexInputs,
            VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2);
  EXPECT_EQ(Pipe->vertexRobustness().Images,
            VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DEVICE_DEFAULT);
  EXPECT_EQ(Pipe->fragmentRobustness().Images,
            VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DISABLED);

  vkDestroyPipeline(Device, Handle, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// An out-of-range behavior value in either the pipeline-level or a
/// stage-level `VkPipelineRobustnessCreateInfo` must fail pipeline
/// creation.
TEST_F(GraphicsPipelineTest, RejectsInvalidPipelineRobustnessBehavior) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkPipelineRobustnessCreateInfo Robustness{};
  Robustness.sType = VK_STRUCTURE_TYPE_PIPELINE_ROBUSTNESS_CREATE_INFO;
  Robustness.images = static_cast<VkPipelineRobustnessImageBehavior>(0xFFFF);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
  Stages[1].pNext = &Robustness;

  VkPipeline Handle = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Handle), VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Handle, VK_NULL_HANDLE);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H4b: a real, four-stage `VK_PRIMITIVE_TOPOLOGY_PATCH_LIST`
/// pipeline -- vertex, tessellation-control (splitting into control-point
/// and patch-constant phases at a real SPIR-V-imported
/// `spirv.ControlBarrier`), tessellation-evaluation, and fragment -- must
/// be accepted, and the resulting executor pipeline must carry all three
/// tessellation-stage `CompiledStage`s and the merged `TessellationState`
/// (`patchControlPoints` from the pipeline, `OutputControlPointCount`/
/// `Domain`/`Partitioning`/`OutputPrimitive` from the two stages' own
/// reflection).
TEST_F(GraphicsPipelineTest, AcceptsTessellationStages) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule TessControl = createModule(TessControlSource);
  VkShaderModule TessEval = createModule(TessEvalSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info =
      makeTessellationCreateInfo(Vertex, TessControl, TessEval, Fragment);

  VkPipeline Handle = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Handle), VK_SUCCESS);

  auto *Pipe = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Handle));
  const feme::graphics::GraphicsPipeline Executor =
      Pipe->buildExecutorPipeline(DynamicGraphicsState{});
  ASSERT_TRUE(Executor.hasTessellationStages());
  EXPECT_EQ(Executor.getTopology(),
            feme::graphics::PrimitiveTopology::PatchList);
  EXPECT_EQ(Executor.getTessellationState().InputControlPointCount, 3u);
  EXPECT_EQ(Executor.getTessellationState().OutputControlPointCount, 3u);
  EXPECT_EQ(Executor.getTessellationState().Domain,
            feme::graphics::TessellatorDomain::Triangle);
  EXPECT_EQ(Executor.getTessellationState().Partitioning,
            feme::graphics::TessPartitioning::FractionalOdd);
  EXPECT_EQ(Executor.getTessellationState().OutputPrimitive,
            feme::graphics::TessOutputPrimitive::TriangleCcw);

  vkDestroyPipeline(Device, Handle, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, TessEval, nullptr);
  vkDestroyShaderModule(Device, TessControl, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H4i: `VkPipelineTessellationDomainOriginStateCreateInfo`
/// chained onto `pTessellationState`'s own `pNext`, requesting
/// `VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT`, must flip the merged
/// `TessellationState::OutputPrimitive` from what `TessEvalSource`'s own
/// `VertexOrderCcw` execution mode would otherwise produce
/// (`TriangleCcw`, confirmed unflipped by `AcceptsTessellationStages`
/// above) to its opposite (`TriangleCw`) -- the tessellator's own winding
/// convention (`Tessellator.cpp`'s `appendTriangle`) is only correct
/// relative to the spec's default, upper-left, domain origin; selecting
/// the lower-left one mirrors the domain's coordinate frame, which
/// reverses every generated triangle's winding as a side effect (a
/// mirror transform always reverses 2D orientation), so this restores
/// the shader's own declared vertex order relative to whichever domain
/// origin the pipeline actually requested.
TEST_F(GraphicsPipelineTest, FlipsTessellationWindingForLowerLeftDomainOrigin) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule TessControl = createModule(TessControlSource);
  VkShaderModule TessEval = createModule(TessEvalSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info =
      makeTessellationCreateInfo(Vertex, TessControl, TessEval, Fragment);
  VkPipelineTessellationDomainOriginStateCreateInfo DomainOrigin{};
  DomainOrigin.sType =
      VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_DOMAIN_ORIGIN_STATE_CREATE_INFO;
  DomainOrigin.domainOrigin = VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT;
  Tessellation.pNext = &DomainOrigin;

  VkPipeline Handle = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Handle), VK_SUCCESS);

  auto *Pipe = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Handle));
  const feme::graphics::GraphicsPipeline Executor =
      Pipe->buildExecutorPipeline(DynamicGraphicsState{});
  EXPECT_EQ(Executor.getTessellationState().OutputPrimitive,
            feme::graphics::TessOutputPrimitive::TriangleCw);

  vkDestroyPipeline(Device, Handle, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, TessEval, nullptr);
  vkDestroyShaderModule(Device, TessControl, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H4i: an *explicit*
/// `VK_TESSELLATION_DOMAIN_ORIGIN_UPPER_LEFT` must behave exactly like
/// omitting `VkPipelineTessellationDomainOriginStateCreateInfo` entirely
/// (`AcceptsTessellationStages`'s own unflipped `TriangleCcw`) -- confirms
/// `hasLowerLeftTessellationDomainOrigin` (GraphicsPipeline.cpp) checks
/// the chained struct's own `domainOrigin` field rather than merely its
/// presence.
TEST_F(GraphicsPipelineTest,
       KeepsTessellationWindingForExplicitUpperLeftDomainOrigin) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule TessControl = createModule(TessControlSource);
  VkShaderModule TessEval = createModule(TessEvalSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info =
      makeTessellationCreateInfo(Vertex, TessControl, TessEval, Fragment);
  VkPipelineTessellationDomainOriginStateCreateInfo DomainOrigin{};
  DomainOrigin.sType =
      VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_DOMAIN_ORIGIN_STATE_CREATE_INFO;
  DomainOrigin.domainOrigin = VK_TESSELLATION_DOMAIN_ORIGIN_UPPER_LEFT;
  Tessellation.pNext = &DomainOrigin;

  VkPipeline Handle = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Handle), VK_SUCCESS);

  auto *Pipe = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Handle));
  const feme::graphics::GraphicsPipeline Executor =
      Pipe->buildExecutorPipeline(DynamicGraphicsState{});
  EXPECT_EQ(Executor.getTessellationState().OutputPrimitive,
            feme::graphics::TessOutputPrimitive::TriangleCcw);

  vkDestroyPipeline(Device, Handle, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, TessEval, nullptr);
  vkDestroyShaderModule(Device, TessControl, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H4d: a tessellation-control entry point writing more than one
/// element of a bare (non-block) array-typed `BuiltIn` output --
/// `TessControlMultiElementArraySource`'s own `gl_TessLevelOuter`-shaped
/// global -- must compile (and JIT-link) successfully end to end: every
/// element's store, not just the first, has to resolve to a real
/// `feme.stage.output.store` rather than leaving a dangling reference to
/// the (never-defined) SPIR-V-imported global.
TEST_F(GraphicsPipelineTest,
       AcceptsTessellationControlMultiElementArrayOutput) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule TessControl = createModule(TessControlMultiElementArraySource);
  VkShaderModule TessEval = createModule(TessEvalSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info =
      makeTessellationCreateInfo(Vertex, TessControl, TessEval, Fragment);

  VkPipeline Handle = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Handle), VK_SUCCESS);

  vkDestroyPipeline(Device, Handle, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, TessEval, nullptr);
  vkDestroyShaderModule(Device, TessControl, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H9c: a tessellation-control entry point whose patch-constant
/// (post-barrier) tessellation-factor store is guarded by
/// `if (gl_InvocationID == 0)` -- `TessControlMaskedPatchConstantStoreSource`
/// -- must still compile: this is the real shape every genuine
/// `dEQP-VK.tessellation.*`/pipeline-statistics tessellation-control
/// shader's patch-constant function takes.
TEST_F(GraphicsPipelineTest,
       AcceptsTessellationControlMaskedPatchConstantStore) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule TessControl =
      createModule(TessControlMaskedPatchConstantStoreSource);
  VkShaderModule TessEval = createModule(TessEvalSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info =
      makeTessellationCreateInfo(Vertex, TessControl, TessEval, Fragment);

  VkPipeline Handle = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Handle), VK_SUCCESS);

  vkDestroyPipeline(Device, Handle, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, TessEval, nullptr);
  vkDestroyShaderModule(Device, TessControl, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H9c: the real, barrierless, mixed control-point/patch-constant
/// shape (`TessControlBarrierlessMixedStoreSource`) every genuine
/// `dEQP-VK.tessellation.*`/pipeline-statistics tessellation-control
/// shader actually compiles to must still compile.
TEST_F(GraphicsPipelineTest, AcceptsTessellationControlBarrierlessMixedStore) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule TessControl =
      createModule(TessControlBarrierlessMixedStoreSource);
  VkShaderModule TessEval = createModule(TessEvalSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info =
      makeTessellationCreateInfo(Vertex, TessControl, TessEval, Fragment);

  VkPipeline Handle = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Handle), VK_SUCCESS);

  vkDestroyPipeline(Device, Handle, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, TessEval, nullptr);
  vkDestroyShaderModule(Device, TessControl, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H9c: the real, barrierless, dynamically-vertex-indexed mixed
/// control-point/patch-constant shape (`TessControlBarrierlessDynamic
/// VertexIndexedMixedStoreSource`) every genuine `dEQP-VK.query_pool.
/// statistics_query.clipping_invocations.*_tessellation*` tessellation-
/// control shader actually compiles to must still compile.
TEST_F(GraphicsPipelineTest,
       AcceptsTessellationControlBarrierlessDynamicVertexIndexedMixedStore) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule TessControl =
      createModule(TessControlBarrierlessDynamicVertexIndexedMixedStoreSource);
  VkShaderModule TessEval = createModule(TessEvalSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info =
      makeTessellationCreateInfo(Vertex, TessControl, TessEval, Fragment);

  VkPipeline Handle = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Handle), VK_SUCCESS);

  vkDestroyPipeline(Device, Handle, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, TessEval, nullptr);
  vkDestroyShaderModule(Device, TessControl, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H4h: a tessellation pipeline whose vertex stage is genuinely
/// empty (`EmptyVertexSource`, no stage-IO globals at all) must still be
/// accepted, since a tessellation-evaluation stage present in the pipeline
/// is the one whose own `SV_Position` output the rasterizer reads --
/// exactly `dEQP-VK.tessellation.winding.*`'s own real shape, previously
/// rejected with "vertex stage does not write a 4-component SV_Position
/// output" even though the *evaluation* stage (`TessEvalSource`) writes
/// one.
TEST_F(GraphicsPipelineTest, AcceptsTessellationPipelineWithEmptyVertexShader) {
  VkShaderModule Vertex = createModule(EmptyVertexSource);
  VkShaderModule TessControl = createModule(TessControlSource);
  VkShaderModule TessEval = createModule(TessEvalSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info =
      makeTessellationCreateInfo(Vertex, TessControl, TessEval, Fragment);

  VkPipeline Handle = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Handle), VK_SUCCESS);

  vkDestroyPipeline(Device, Handle, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, TessEval, nullptr);
  vkDestroyShaderModule(Device, TessControl, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H4h: without a tessellation-evaluation stage, an empty vertex
/// shader is still rejected -- the ordinary vertex -> fragment pipeline has
/// no other producer of a rasterizer-visible position, so the pre-existing
/// `SV_Position` requirement on the vertex stage is unaffected by this
/// milestone's tessellation-specific relaxation.
TEST_F(GraphicsPipelineTest, RejectsEmptyVertexShaderWithoutTessellation) {
  VkShaderModule Vertex = createModule(EmptyVertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);

  VkPipeline Handle = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Handle), VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Handle, VK_NULL_HANDLE);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H4b: a tessellation-control stage without its
/// tessellation-evaluation sibling (and vice versa) is rejected -- neither
/// has a tessellator state to run with alone.
TEST_F(GraphicsPipelineTest, RejectsUnpairedTessellationStage) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule TessControl = createModule(TessControlSource);
  VkShaderModule TessEval = createModule(TessEvalSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info =
      makeTessellationCreateInfo(Vertex, TessControl, TessEval, Fragment);
  Info.stageCount = 3;
  VkPipelineShaderStageCreateInfo OnlyControl[3] = {
      TessStages[0], TessStages[1], TessStages[3]};
  Info.pStages = OnlyControl;
  VkPipeline Pipe = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Pipe, VK_NULL_HANDLE);

  VkPipelineShaderStageCreateInfo OnlyEval[3] = {TessStages[0], TessStages[2],
                                                 TessStages[3]};
  Info.pStages = OnlyEval;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Pipe, VK_NULL_HANDLE);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, TessEval, nullptr);
  vkDestroyShaderModule(Device, TessControl, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H4b: `VK_PRIMITIVE_TOPOLOGY_PATCH_LIST` requires a
/// tessellation-control/evaluation stage pair, and that pair requires
/// `VK_PRIMITIVE_TOPOLOGY_PATCH_LIST` -- each direction of the mismatch is
/// rejected.
TEST_F(GraphicsPipelineTest, RejectsTopologyTessellationStageMismatch) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule TessControl = createModule(TessControlSource);
  VkShaderModule TessEval = createModule(TessEvalSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  // A tessellating pipeline naming a non-patch-list topology.
  VkGraphicsPipelineCreateInfo Info =
      makeTessellationCreateInfo(Vertex, TessControl, TessEval, Fragment);
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipeline Pipe = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Pipe, VK_NULL_HANDLE);

  // A patch-list topology with no tessellation stages at all.
  VkGraphicsPipelineCreateInfo NonTessInfo = makeCreateInfo(Vertex, Fragment);
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
  EXPECT_EQ(create(NonTessInfo, Pipe), VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Pipe, VK_NULL_HANDLE);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, TessEval, nullptr);
  vkDestroyShaderModule(Device, TessControl, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H4b: a tessellating pipeline needs
/// `VkPipelineTessellationStateCreateInfo`, and its `patchControlPoints`
/// must be in `[1, maxTessellationPatchSize]` (32, `feme::graphics::
/// MaxPatchControlPoints`) -- zero, and one past the limit, are both
/// rejected; missing the struct entirely is too.
TEST_F(GraphicsPipelineTest, RejectsInvalidPatchControlPoints) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule TessControl = createModule(TessControlSource);
  VkShaderModule TessEval = createModule(TessEvalSource);
  VkShaderModule Fragment = createModule(FragmentSource);
  VkPipeline Pipe = VK_NULL_HANDLE;

  VkGraphicsPipelineCreateInfo Info =
      makeTessellationCreateInfo(Vertex, TessControl, TessEval, Fragment);
  Info.pTessellationState = nullptr;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Pipe, VK_NULL_HANDLE);

  Info = makeTessellationCreateInfo(Vertex, TessControl, TessEval, Fragment);
  Tessellation.patchControlPoints = 0;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Pipe, VK_NULL_HANDLE);

  Info = makeTessellationCreateInfo(Vertex, TessControl, TessEval, Fragment);
  Tessellation.patchControlPoints = feme::graphics::MaxPatchControlPoints + 1;
  EXPECT_EQ(create(Info, Pipe), VK_ERROR_INITIALIZATION_FAILED);
  EXPECT_EQ(Pipe, VK_NULL_HANDLE);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, TessEval, nullptr);
  vkDestroyShaderModule(Device, TessControl, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H5e: `vkCreateGraphicsPipelines` now accepts
/// `VK_SHADER_STAGE_GEOMETRY_BIT`, compiling the module into a
/// `feme::ShaderStage::Geometry` `CompiledStage` and reflecting its
/// declared input/output primitive class, invocation count and maximum
/// output vertex count into `graphics::GraphicsPipeline::
/// setGeometryStage`/`getGeometryState` (which the executor has consumed
/// since roadmap H5d).
TEST_F(GraphicsPipelineTest, AcceptsGeometryStage) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Geometry = createModule(GeometrySource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info =
      makeGeometryCreateInfo(Vertex, Geometry, Fragment);

  VkPipeline Handle = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Handle), VK_SUCCESS);

  auto *Pipe = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Handle));
  EXPECT_TRUE(Pipe->hasGeometryStages());
  const feme::graphics::GraphicsPipeline Executor =
      Pipe->buildExecutorPipeline(DynamicGraphicsState{});
  ASSERT_TRUE(Executor.hasGeometryStages());
  EXPECT_EQ(Executor.getGeometryStage().getStage(),
            feme::ShaderStage::Geometry);
  EXPECT_EQ(Executor.getGeometryState().InputPrimitive,
            feme::graphics::GeometryInputPrimitive::Triangles);
  EXPECT_EQ(Executor.getGeometryState().OutputPrimitive,
            feme::graphics::GeometryOutputPrimitive::TriangleStrip);
  EXPECT_EQ(Executor.getGeometryState().MaxOutputVertices, 3u);
  EXPECT_EQ(Executor.getGeometryState().Invocations, 1u);
  // `TriangleList` was left unchanged from `makeCreateInfo`'s own default
  // -- a geometry stage does not, by itself, require an adjacency
  // topology (`AcceptsAdjacencyTopologyWithGeometryStage` below covers the
  // topology this milestone actually adds).
  EXPECT_EQ(Executor.getTopology(),
            feme::graphics::PrimitiveTopology::TriangleList);

  vkDestroyPipeline(Device, Handle, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Geometry, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H5e: the four `*_WITH_ADJACENCY` topologies, previously
/// rejected unconditionally by `mapTopology`, now succeed once a geometry
/// stage is bound -- the executor (roadmap H5d) has been ready to consume
/// adjacency-assembled primitives all along. (Roadmap H7l:
/// `AcceptsAdjacencyTopologyWithoutGeometryStage` below covers the
/// other, also-legal combination -- a geometry stage is never required.)
TEST_F(GraphicsPipelineTest, AcceptsAdjacencyTopologyWithGeometryStage) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Geometry = createModule(GeometrySource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info =
      makeGeometryCreateInfo(Vertex, Geometry, Fragment);
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;

  VkPipeline Handle = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Handle), VK_SUCCESS);

  auto *Pipe = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Handle));
  const feme::graphics::GraphicsPipeline Executor =
      Pipe->buildExecutorPipeline(DynamicGraphicsState{});
  EXPECT_EQ(Executor.getTopology(),
            feme::graphics::PrimitiveTopology::TriangleListWithAdjacency);

  vkDestroyPipeline(Device, Handle, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Geometry, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H7l: every one of the four adjacency topologies is accepted
/// *without* a bound geometry stage too -- `VUID-VkGraphicsPipelineCreate
/// Info-topology-00738`/neighbors require the `geometryShader` *device
/// feature* (this ICD always advertises it), not that this particular
/// pipeline itself binds a geometry stage; `AcceptsAdjacencyTopologyWith
/// GeometryStage` above covers the other, also-legal combination. Found
/// via a real `dEQP-VK.clipping.clip_volume.depth_clamp.{triangle,line}_
/// *_with_adjacency` reproduction, whose own vertex/fragment-only
/// pipelines are exactly this combination -- `vkCreateGraphicsPipelines`
/// used to fail all 4 with `VK_ERROR_INITIALIZATION_FAILED` before this
/// fix. `Executor.cpp`'s own runtime rejection of the same combination
/// (roadmap H5d) was corrected alongside this row; see
/// `ExecutorTest.cpp`'s `Renders{TriangleList,LineList}WithAdjacencyCore
/// {Triangle,Line}WithoutAGeometryStage`.
TEST_F(GraphicsPipelineTest, AcceptsAdjacencyTopologyWithoutGeometryStage) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  static constexpr VkPrimitiveTopology AdjacencyTopologies[] = {
      VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY,
      VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY,
      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY,
      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY,
  };
  static constexpr feme::graphics::PrimitiveTopology ExpectedTopologies[] = {
      feme::graphics::PrimitiveTopology::LineListWithAdjacency,
      feme::graphics::PrimitiveTopology::LineStripWithAdjacency,
      feme::graphics::PrimitiveTopology::TriangleListWithAdjacency,
      feme::graphics::PrimitiveTopology::TriangleStripWithAdjacency,
  };
  for (size_t I = 0;
       I != sizeof(AdjacencyTopologies) / sizeof(AdjacencyTopologies[0]); ++I) {
    VkGraphicsPipelineCreateInfo Info = makeCreateInfo(Vertex, Fragment);
    InputAssembly.topology = AdjacencyTopologies[I];
    VkPipeline Handle = VK_NULL_HANDLE;
    ASSERT_EQ(create(Info, Handle), VK_SUCCESS)
        << "topology " << AdjacencyTopologies[I];

    auto *Pipe = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Handle));
    const feme::graphics::GraphicsPipeline Executor =
        Pipe->buildExecutorPipeline(DynamicGraphicsState{});
    EXPECT_EQ(Executor.getTopology(), ExpectedTopologies[I])
        << "topology " << AdjacencyTopologies[I];

    vkDestroyPipeline(Device, Handle, nullptr);
  }

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H5e-b: `Executor.cpp`'s `executeDraws` implements
/// `primitiveRestartEnable` for every strip/fan topology
/// `feme::graphics::topologySupportsPrimitiveRestart` lists, not just
/// `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP` -- `AcceptsPrimitiveRestartOn
/// TriangleStrip` above already covers that one; this covers the
/// remaining four, including the two adjacency topologies (bound to a
/// geometry stage here since H7l's own `AcceptsAdjacencyTopologyWithout
/// GeometryStage` above already covers the geometry-stage-free
/// combination; either is legal). Before this fix,
/// `GraphicsPipeline.cpp`'s creation-time gate rejected every one of
/// these four with `VK_ERROR_INITIALIZATION_FAILED` and no diagnostic
/// (`RejectsUnimplementedStateCombinations`'s own default-topology case
/// above is unaffected: `TriangleList` still correctly rejects restart).
TEST_F(GraphicsPipelineTest, AcceptsPrimitiveRestartOnStripAndFanTopologies) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Geometry = createModule(GeometrySource);
  VkShaderModule Fragment = createModule(FragmentSource);

  auto TestTopology = [&](VkPrimitiveTopology Topology, bool NeedsGeometry) {
    VkGraphicsPipelineCreateInfo Info =
        NeedsGeometry ? makeGeometryCreateInfo(Vertex, Geometry, Fragment)
                      : makeCreateInfo(Vertex, Fragment);
    InputAssembly.topology = Topology;
    InputAssembly.primitiveRestartEnable = VK_TRUE;
    VkPipeline Pipe = VK_NULL_HANDLE;
    EXPECT_EQ(create(Info, Pipe), VK_SUCCESS) << "topology " << Topology;
    if (Pipe != VK_NULL_HANDLE)
      vkDestroyPipeline(Device, Pipe, nullptr);
  };

  TestTopology(VK_PRIMITIVE_TOPOLOGY_LINE_STRIP, /*NeedsGeometry=*/false);
  TestTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN, /*NeedsGeometry=*/false);
  TestTopology(VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY,
               /*NeedsGeometry=*/true);
  TestTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY,
               /*NeedsGeometry=*/true);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Geometry, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// Roadmap H5e-b: a geometry entry point that emits no vertices at all
/// (`dEQP-VK.geometry.emit.*_emit_0_end_0`'s degenerate shape) has an
/// entirely empty signature -- SPIR-V only lists an entry point's *used*
/// interface variables -- rather than one merely missing `SV_Position`.
/// Before this fix, `validateStageInterfaces` rejected this
/// unconditionally ("the geometry stage does not write a 4-component
/// SV_Position output"), and the fragment stage's own unmatched
/// location-0 varying input would have been rejected too ("fragment
/// input location 0 has no matching vertex stage output") once that
/// first check was relaxed. Nothing is ever rasterized from a stage that
/// emits nothing, regardless of whether it wrote a position or any
/// varying, so pipeline creation must succeed.
TEST_F(GraphicsPipelineTest, AcceptsGeometryStageThatNeverEmits) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Geometry = createModule(EmptyGeometrySource);
  VkShaderModule Fragment = createModule(VaryingFragmentSource);

  VkGraphicsPipelineCreateInfo Info =
      makeGeometryCreateInfo(Vertex, Geometry, Fragment);

  VkPipeline Handle = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Handle), VK_SUCCESS);
  ASSERT_NE(Handle, VK_NULL_HANDLE);

  vkDestroyPipeline(Device, Handle, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Geometry, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap H6f) `vkCreateGraphicsPipelines` accepts a mesh pipeline with
/// no task stage at all -- the mesh stage is dispatched directly, per
/// `PreparedDraw::MeshDraws`' own group count (`vkCmdDrawMeshTasksEXT`'s
/// shape, roadmap H6e).
TEST_F(GraphicsPipelineTest, AcceptsMeshPipelineWithNoTaskStage) {
  VkShaderModule Mesh = createModule(MeshSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeMeshCreateInfo(Mesh, Fragment);

  VkPipeline Handle = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Handle), VK_SUCCESS);
  ASSERT_NE(Handle, VK_NULL_HANDLE);

  auto *Pipe = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Handle));
  EXPECT_TRUE(Pipe->hasMeshStages());
  EXPECT_FALSE(Pipe->hasTaskStage());
  const feme::graphics::GraphicsPipeline Executor =
      Pipe->buildExecutorPipeline(DynamicGraphicsState{});
  ASSERT_TRUE(Executor.hasMeshStages());
  EXPECT_FALSE(Executor.hasTaskStage());
  EXPECT_EQ(Executor.getMeshState().OutputTopology,
            feme::graphics::MeshOutputTopology::Triangles);
  EXPECT_EQ(Executor.getMeshState().MaxOutputVertices, 3u);
  EXPECT_EQ(Executor.getMeshState().MaxOutputPrimitives, 1u);
  // (roadmap H6f) The dispatch limits `buildExecutorPipeline` supplies are
  // this ICD's own honest, enforced ceilings -- the real counterpart of
  // `Executor.cpp`'s previous hardcoded placeholder -- shared with
  // `VkPhysicalDeviceMeshShaderPropertiesEXT`'s advertised
  // `maxMeshWorkGroupCount`/`maxMeshWorkGroupTotalCount`.
  EXPECT_EQ(Executor.getMeshDispatchLimits().MaxGroupCount,
            MaxMeshWorkGroupCount);
  EXPECT_EQ(Executor.getMeshDispatchLimits().MaxTotalGroupCount,
            MaxMeshWorkGroupTotalCount);

  vkDestroyPipeline(Device, Handle, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Mesh, nullptr);
}

/// (roadmap H6f) A mesh pipeline may also declare a task stage; it is
/// only ever legal alongside a mesh stage (`RejectsTaskStageWithoutMesh
/// Stage` below covers the converse).
TEST_F(GraphicsPipelineTest, AcceptsMeshPipelineWithTaskStage) {
  VkShaderModule Mesh = createModule(MeshSource);
  VkShaderModule Task = createModule(TaskSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeMeshCreateInfo(Mesh, Fragment, Task);

  VkPipeline Handle = VK_NULL_HANDLE;
  ASSERT_EQ(create(Info, Handle), VK_SUCCESS);
  ASSERT_NE(Handle, VK_NULL_HANDLE);

  auto *Pipe = static_cast<GraphicsPipeline *>(fromHandle<Pipeline>(Handle));
  EXPECT_TRUE(Pipe->hasMeshStages());
  EXPECT_TRUE(Pipe->hasTaskStage());
  const feme::graphics::GraphicsPipeline Executor =
      Pipe->buildExecutorPipeline(DynamicGraphicsState{});
  ASSERT_TRUE(Executor.hasTaskStage());
  EXPECT_EQ(Executor.getTaskDispatchLimits().MaxGroupCount,
            MaxTaskWorkGroupCount);
  EXPECT_EQ(Executor.getTaskDispatchLimits().MaxTotalGroupCount,
            MaxTaskWorkGroupTotalCount);

  vkDestroyPipeline(Device, Handle, nullptr);
  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Task, nullptr);
  vkDestroyShaderModule(Device, Mesh, nullptr);
}

/// (roadmap H6f) A mesh pipeline may not also declare a vertex stage --
/// the two are mutually exclusive ways to originate a pipeline's
/// vertices.
TEST_F(GraphicsPipelineTest, RejectsMeshAndVertexStageCombination) {
  VkShaderModule Vertex = createModule(VertexSource);
  VkShaderModule Mesh = createModule(MeshSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeMeshCreateInfo(Mesh, Fragment);
  VkPipelineShaderStageCreateInfo Combined[3]{};
  Combined[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Combined[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  Combined[0].module = Vertex;
  Combined[0].pName = "main";
  Combined[1] = MeshStages[0]; // the mesh stage `makeMeshCreateInfo` built.
  Combined[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Combined[2].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  Combined[2].module = Fragment;
  Combined[2].pName = "main";
  Info.stageCount = 3;
  Info.pStages = Combined;

  VkPipeline Handle = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Handle), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Mesh, nullptr);
  vkDestroyShaderModule(Device, Vertex, nullptr);
}

/// (roadmap H6f) A task stage only ever drives a mesh stage's dispatch;
/// it is meaningless (and rejected) without one.
TEST_F(GraphicsPipelineTest, RejectsTaskStageWithoutMeshStage) {
  VkShaderModule Task = createModule(TaskSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkPipelineShaderStageCreateInfo TaskOnlyStages[2]{};
  TaskOnlyStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  TaskOnlyStages[0].stage = VK_SHADER_STAGE_TASK_BIT_EXT;
  TaskOnlyStages[0].module = Task;
  TaskOnlyStages[0].pName = "main";
  TaskOnlyStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  TaskOnlyStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  TaskOnlyStages[1].module = Fragment;
  TaskOnlyStages[1].pName = "main";

  VkGraphicsPipelineCreateInfo Info{};
  Info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  Info.stageCount = 2;
  Info.pStages = TaskOnlyStages;
  Info.pVertexInputState = nullptr;
  Info.pInputAssemblyState = nullptr;
  Viewport = {0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
  Scissor = {{0, 0}, {4, 4}};
  ViewportState = {};
  ViewportState.viewportCount = 1;
  ViewportState.pViewports = &Viewport;
  ViewportState.scissorCount = 1;
  ViewportState.pScissors = &Scissor;
  Raster = {};
  Raster.cullMode = VK_CULL_MODE_NONE;
  Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  Raster.polygonMode = VK_POLYGON_MODE_FILL;
  Raster.lineWidth = 1.0f;
  Multisample = {};
  Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  BlendAttachment = {};
  BlendAttachment.colorWriteMask = 0xF;
  Blend = {};
  Blend.attachmentCount = 1;
  Blend.pAttachments = &BlendAttachment;
  Info.pViewportState = &ViewportState;
  Info.pRasterizationState = &Raster;
  Info.pMultisampleState = &Multisample;
  Info.pColorBlendState = &Blend;
  Info.layout = Layout;
  Info.renderPass = Pass;

  VkPipeline Handle = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Handle), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Task, nullptr);
}

/// (roadmap H6g-b) A mesh pipeline's `pVertexInputState` is spec-ignored,
/// not rejected -- a non-null one (as a real caller's shared
/// `makeGraphicsPipeline`-style helper may well pass, unconditionally,
/// with no mesh-aware carve-out of its own) must still be accepted.
TEST_F(GraphicsPipelineTest, AcceptsMeshPipelineWithVertexInputState) {
  VkShaderModule Mesh = createModule(MeshSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeMeshCreateInfo(Mesh, Fragment);
  VertexInput = {};
  VertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  Info.pVertexInputState = &VertexInput;

  VkPipeline Handle = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Handle), VK_SUCCESS);
  vkDestroyPipeline(Device, Handle, nullptr);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Mesh, nullptr);
}

/// (roadmap H6g-b) Symmetric case for `pInputAssemblyState`: also
/// spec-ignored, not rejected, for a mesh pipeline.
TEST_F(GraphicsPipelineTest, AcceptsMeshPipelineWithInputAssemblyState) {
  VkShaderModule Mesh = createModule(MeshSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeMeshCreateInfo(Mesh, Fragment);
  InputAssembly = {};
  InputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  Info.pInputAssemblyState = &InputAssembly;

  VkPipeline Handle = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Handle), VK_SUCCESS);
  vkDestroyPipeline(Device, Handle, nullptr);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Mesh, nullptr);
}

/// (roadmap H6f) `maxMeshOutputVertices`/`maxMeshOutputPrimitives` are
/// enforced at pipeline creation, not left as an unchecked, merely-
/// advertised ceiling (mirroring `maxGeometryOutputVertices`'s own
/// enforcement, H5e).
TEST_F(GraphicsPipelineTest, RejectsMeshOutputCountsExceedingLimits) {
  // `MaxMeshOutputVertices` (256) declared as an `OutputVertices` of 257
  // -- one past the honest ceiling this ICD advertises and enforces.
  constexpr llvm::StringLiteral OverLimitMeshSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [MeshShadingEXT], [SPV_EXT_mesh_shader]> {
  spirv.func @main() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "MeshEXT" @main
  spirv.ExecutionMode @main "OutputTrianglesEXT"
  spirv.ExecutionMode @main "OutputVertices", 257
  spirv.ExecutionMode @main "OutputPrimitivesEXT", 1
  spirv.ExecutionMode @main "LocalSize", 1, 1, 1
}
)mlir";
  VkShaderModule Mesh = createModule(OverLimitMeshSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeMeshCreateInfo(Mesh, Fragment);

  VkPipeline Handle = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Handle), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Mesh, nullptr);
}

/// (roadmap H6f) `maxMeshWorkGroupSize`/`maxMeshWorkGroupInvocations` are
/// enforced at pipeline creation too (`validateMeshOrTaskGroupSize`,
/// `GraphicsPipeline.cpp`), mirroring the compute pipeline path's own
/// `maxComputeWorkGroupSize`/`Invocations` check (`Pipeline.cpp`) -- a
/// mesh entry's declared `LocalSize` one past the advertised ceiling
/// (128 in every dimension) must be rejected, not silently accepted.
TEST_F(GraphicsPipelineTest, RejectsMeshWorkGroupSizeExceedingLimits) {
  constexpr llvm::StringLiteral OverLimitLocalSizeMeshSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [MeshShadingEXT], [SPV_EXT_mesh_shader]> {
  spirv.func @main() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "MeshEXT" @main
  spirv.ExecutionMode @main "OutputTrianglesEXT"
  spirv.ExecutionMode @main "OutputVertices", 1
  spirv.ExecutionMode @main "OutputPrimitivesEXT", 1
  spirv.ExecutionMode @main "LocalSize", 129, 1, 1
}
)mlir";
  VkShaderModule Mesh = createModule(OverLimitLocalSizeMeshSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeMeshCreateInfo(Mesh, Fragment);

  VkPipeline Handle = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Handle), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Mesh, nullptr);
}

/// (roadmap H6f) The task stage's own counterpart to the previous test:
/// `maxTaskWorkGroupSize`/`maxTaskWorkGroupInvocations` are enforced for a
/// task entry point exactly the way they are for a mesh one.
TEST_F(GraphicsPipelineTest, RejectsTaskWorkGroupSizeExceedingLimits) {
  constexpr llvm::StringLiteral OverLimitLocalSizeTaskSource = R"mlir(
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [MeshShadingEXT], [SPV_EXT_mesh_shader]> {
  spirv.func @main() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "TaskEXT" @main
  spirv.ExecutionMode @main "LocalSize", 1, 1, 129
}
)mlir";
  VkShaderModule Mesh = createModule(MeshSource);
  VkShaderModule Task = createModule(OverLimitLocalSizeTaskSource);
  VkShaderModule Fragment = createModule(FragmentSource);

  VkGraphicsPipelineCreateInfo Info = makeMeshCreateInfo(Mesh, Fragment, Task);

  VkPipeline Handle = VK_NULL_HANDLE;
  EXPECT_EQ(create(Info, Handle), VK_ERROR_INITIALIZATION_FAILED);

  vkDestroyShaderModule(Device, Fragment, nullptr);
  vkDestroyShaderModule(Device, Task, nullptr);
  vkDestroyShaderModule(Device, Mesh, nullptr);
}

} // namespace
