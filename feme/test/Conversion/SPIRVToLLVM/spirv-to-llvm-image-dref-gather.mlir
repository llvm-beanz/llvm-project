// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file --mlir-very-unsafe-disable-verifier-on-parsing %s | FileCheck %s

// Roadmap L7d: `spirv.ImageDrefGather` with `None` image operands (what
// `dxc` emits for `Texture2D<T>::GatherCmp(sampler, coord, dref)`, confirmed
// via a real repro compiled with `dxc -fspv-target-env=vulkan1.3`) converts
// to the pre-existing (but previously unreachable from MLIR's own SPIR-V
// dialect) `llvm.spv.resource.gather.cmp` intrinsic LLVM's SPIRV backend's
// own `selectGatherIntrinsic` already selects `OpSampledImage`+
// `OpImageDrefGather` from, threading a defaulted zero offset through the
// same way `ImageSampleDrefImplicitLodPattern` does for an ordinary
// depth-comparison sample.

// CHECK-LABEL: llvm.func @gathercmp
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: %[[OFFSET:.*]] = llvm.mlir.constant(dense<0> : vector<2xi32>) : vector<2xi32>
// CHECK: llvm.call_intrinsic "llvm.spv.resource.gather.cmp"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %[[OFFSET]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @gathercmp(%coord : vector<2xf32>, %dref : f32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageDrefGather %4, %coord, %dref : !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32 -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// Roadmap L7d: `spirv.ImageDrefGather` with a real `ConstOffset` image
// operand threads the real offset through instead of the defaulted zero
// offset the unmodified case above uses -- mirroring
// `spirv-to-llvm-sample-dref-and-query-lod.mlir`'s own
// `samplecmp_const_offset_minlod` case for the depth-comparison *sample*
// sibling pattern (`spirv.ImageDrefGather` itself has no `Bias`/`Lod`/
// `Grad`/`MinLod` image operand at all, unlike a sample -- a gather
// instruction always operates at mip level 0 per the SPIR-V spec).

// CHECK-LABEL: llvm.func @gathercmp_const_offset
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: llvm.call_intrinsic "llvm.spv.resource.gather.cmp"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %[[OFFSET:.*]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @gathercmp_const_offset(%coord : vector<2xf32>, %dref : f32, %offset : vector<2xsi32>) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageDrefGather %4, %coord, %dref ["ConstOffset"], %offset : !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32, vector<2xsi32> -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// Roadmap L202: `spirv.ImageDrefGather` with the plain (non-constant)
// `Offset` image operand -- glslang's own `textureGatherOffset` lowering
// for a depth-comparison gather when the offset argument isn't a
// compile-time constant, confirmed via a real repro reduced from
// `dEQP-VK.glsl.texture_gather.compute.offset.implementation_offset.2d.depth32f.*`.
// Mirrors `gather_dynamic_offset` above: `%offset` is an ordinary SSA
// value, threaded through the exact same intrinsic operand
// `gathercmp_const_offset` above threads its own literal `%offset`
// through.

// CHECK-LABEL: llvm.func @gathercmp_dynamic_offset
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: llvm.call_intrinsic "llvm.spv.resource.gather.cmp"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %[[OFFSET:.*]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @gathercmp_dynamic_offset(%coord : vector<2xf32>, %dref : f32, %offset : vector<2xsi32>) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageDrefGather %4, %coord, %dref ["Offset"], %offset : !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32, vector<2xsi32> -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// Roadmap L202(a): `spirv.ImageDrefGather` with a `ConstOffsets` (plural)
// image operand -- `ImageDrefGatherPattern` never received the same
// `ConstOffsets` widening `ImageGatherPattern` got under L125(m)/L125(n),
// a gap first flagged (but not filed) by L125(k)'s own closing text and
// concretely confirmed by a real `dEQP-VK.glsl.texture_gather.graphics.
// offsets.*` depth-format repro. Mirrors `gather_const_offsets` in
// `spirv-to-llvm-image-gather.mlir` exactly (same flattening of the
// spec-mandated `array<4 x vector<2xsi32>>` into one wide `vector<8xi32>`,
// via the shared `flattenConstOffsetsArray` helper both patterns now
// call), but threads a float `%dref` through in place of an integer
// `%component`.

// CHECK-LABEL: llvm.func @gathercmp_const_offsets
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
// CHECK-NEXT: llvm.call_intrinsic "llvm.spv.resource.gather.cmp"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %[[FLAT]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, ImageGatherExtended], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @gathercmp_const_offsets(%coord : vector<2xf32>, %dref : f32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %o0 = spirv.Constant dense<[1, 2]> : vector<2xsi32>
    %o1 = spirv.Constant dense<[3, 4]> : vector<2xsi32>
    %o2 = spirv.Constant dense<[5, 6]> : vector<2xsi32>
    %o3 = spirv.Constant dense<[7, 8]> : vector<2xsi32>
    %offsets = spirv.CompositeConstruct %o0, %o1, %o2, %o3 : (vector<2xsi32>, vector<2xsi32>, vector<2xsi32>, vector<2xsi32>) -> !spirv.array<4 x vector<2xsi32>>
    %5 = spirv.ImageDrefGather %4, %coord, %dref ["ConstOffsets"], %offsets : !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32, !spirv.array<4 x vector<2xsi32>> -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}
