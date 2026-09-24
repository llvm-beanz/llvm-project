// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file --mlir-very-unsafe-disable-verifier-on-parsing %s | FileCheck %s

// Roadmap L7g: `spirv.ImageGather` with `None` image operands (what `dxc`
// emits for `Texture2D<T>::Gather{,Red,Green,Blue,Alpha}(sampler, coord)`,
// confirmed via a real repro compiled with `dxc -fspv-target-env=vulkan1.3`)
// converts to the pre-existing (but previously unreachable from MLIR's own
// SPIR-V dialect, which had no `spirv.ImageGather` op at all until this same
// row added it) `llvm.spv.resource.gather` intrinsic LLVM's SPIRV backend's
// own `selectGatherIntrinsic` already selects `OpSampledImage`+
// `OpImageGather` from, threading a defaulted zero offset through the same
// way `ImageDrefGatherPattern` does for its own depth-comparison-gather
// sibling.

// CHECK-LABEL: llvm.func @gather
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: %[[OFFSET:.*]] = llvm.mlir.constant(dense<0> : vector<2xi32>) : vector<2xi32>
// CHECK: llvm.call_intrinsic "llvm.spv.resource.gather"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %[[OFFSET]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @gather(%coord : vector<2xf32>, %component : i32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageGather %4, %coord, %component : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, i32 -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// Roadmap L7g: `spirv.ImageGather` with a real `ConstOffset` image operand
// threads the real offset through instead of the defaulted zero offset the
// unmodified case above uses -- mirroring
// `spirv-to-llvm-image-dref-gather.mlir`'s own `gathercmp_const_offset`
// case for the depth-comparison-gather sibling pattern (`spirv.ImageGather`
// itself has no `Bias`/`Lod`/`Grad`/`MinLod` image operand at all, just
// like `spirv.ImageDrefGather` -- a gather instruction always operates at
// mip level 0 per the SPIR-V spec).

// CHECK-LABEL: llvm.func @gather_const_offset
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: llvm.call_intrinsic "llvm.spv.resource.gather"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %[[OFFSET:.*]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @gather_const_offset(%coord : vector<2xf32>, %component : i32, %offset : vector<2xsi32>) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageGather %4, %coord, %component ["ConstOffset"], %offset : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, i32, vector<2xsi32> -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// Roadmap L125(m): `spirv.ImageGather` with a `ConstOffsets` (plural)
// image operand -- HLSL `Gather*`'s 4-independent-offset overload,
// `TextureGatherOffsets` in SPIR-V terms, a real glslang-compiled shape
// confirmed via `dEQP-VK.glsl.texture_gather.graphics.offsets.*` -- flattens
// the spec-mandated `array<4 x vector<2xsi32>>` constant into a single
// wider `vector<8xi32>`, scalar by scalar, and threads that through the
// exact same intrinsic call the single-offset `ConstOffset` case above
// does; `%offsets`' own 4 elements are `(1,2)`, `(3,4)`, `(5,6)`, `(7,8)`,
// so the flattened vector's own lanes are checked in that same order.

// CHECK-LABEL: llvm.func @gather_const_offsets
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: %[[E0:.*]] = llvm.extractvalue %{{.*}}[0] : !llvm.array<4 x vector<2xi32>>
// CHECK: %[[L0:.*]] = llvm.extractelement %[[E0]][%{{.*}} : i64] : vector<2xi32>
// CHECK: %{{.*}} = llvm.insertelement %[[L0]], %{{.*}}[%{{.*}} : i64] : vector<8xi32>
// CHECK: %[[E3:.*]] = llvm.extractvalue %{{.*}}[3] : !llvm.array<4 x vector<2xi32>>
// CHECK: llvm.extractelement %[[E3]]
// CHECK: llvm.insertelement
// CHECK: llvm.extractelement %[[E3]]
// CHECK: %[[FLAT:.*]] = llvm.insertelement %{{.*}}, %{{.*}}[%{{.*}} : i64] : vector<8xi32>
// CHECK-NEXT: llvm.call_intrinsic "llvm.spv.resource.gather"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %[[FLAT]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, ImageGatherExtended], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @gather_const_offsets(%coord : vector<2xf32>, %component : i32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %o0 = spirv.Constant dense<[1, 2]> : vector<2xsi32>
    %o1 = spirv.Constant dense<[3, 4]> : vector<2xsi32>
    %o2 = spirv.Constant dense<[5, 6]> : vector<2xsi32>
    %o3 = spirv.Constant dense<[7, 8]> : vector<2xsi32>
    %offsets = spirv.CompositeConstruct %o0, %o1, %o2, %o3 : (vector<2xsi32>, vector<2xsi32>, vector<2xsi32>, vector<2xsi32>) -> !spirv.array<4 x vector<2xsi32>>
    %5 = spirv.ImageGather %4, %coord, %component ["ConstOffsets"], %offsets : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, i32, !spirv.array<4 x vector<2xsi32>> -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}
