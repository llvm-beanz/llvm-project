// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file --mlir-very-unsafe-disable-verifier-on-parsing %s | FileCheck %s

// Roadmap L31: `spirv.ImageSampleDrefImplicitLod` with no image operands
// (what `dxc` emits for `Texture2D<T>::SampleCmp(sampler, coord, dref)`)
// converts to the `llvm.spv.resource.samplecmp` intrinsic LLVM's SPIRV
// backend selects `OpSampledImage`+`OpImageSampleDrefImplicitLod` from
// (see `llvm/test/CodeGen/SPIRV/hlsl-resources/SampleCmp.ll`), threading a
// defaulted zero offset through the same way `ImageSampleImplicitLodPattern`
// does for a plain (non-`Dref`) sample.

// CHECK-LABEL: llvm.func @samplecmp
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: %[[OFFSET:.*]] = llvm.mlir.constant(dense<0> : vector<2xi32>) : vector<2xi32>
// CHECK: llvm.call_intrinsic "llvm.spv.resource.samplecmp"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %[[OFFSET]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @samplecmp(%coord : vector<2xf32>, %dref : f32) -> f32 "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleDrefImplicitLod %4, %coord, %dref : !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32 -> f32
    spirv.ReturnValue %5 : f32
  }
}

// -----

// Roadmap L31: `spirv.ImageSampleDrefImplicitLod` with `ConstOffset|MinLod`
// together converts to `llvm.spv.resource.samplecmp.clamp`, threading the
// real offset and clamp value through instead of the defaulted zero offset
// the unmodified case above uses -- mirroring
// `spirv-to-llvm-sampling.mlir`'s own `sample_const_offset_minlod` case for
// the non-`Dref` sibling pattern. A `Bias` operand selects a different
// intrinsic pair entirely -- see the two `samplecmpbias` cases below.

// CHECK-LABEL: llvm.func @samplecmp_const_offset_minlod
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: llvm.call_intrinsic "llvm.spv.resource.samplecmp.clamp"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %[[OFFSET:.*]], %{{.*}})
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @samplecmp_const_offset_minlod(%coord : vector<2xf32>, %dref : f32, %offset : vector<2xsi32>, %clamp : f32) -> f32 "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleDrefImplicitLod %4, %coord, %dref ["ConstOffset|MinLod"], %offset, %clamp : !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32, vector<2xsi32>, f32 -> f32
    spirv.ReturnValue %5 : f32
  }
}

// -----

// Roadmap L52(b): `spirv.ImageSampleDrefImplicitLod` with a `Bias` operand
// converts to `llvm.spv.resource.samplecmpbias`, the depth-comparison
// counterpart of the non-`Dref` sibling pattern's own
// `llvm.spv.resource.samplebias`. The bias operand lands immediately after
// the dref operand and ahead of the (here defaulted-to-zero) offset,
// matching SPIR-V's own fixed Image Operands bit order. This is what
// GLSL's `texture(sampler2DShadow, coord, bias)` emits; HLSL has no
// spelling for it, so no `llvm/test/CodeGen/SPIRV/hlsl-resources` case
// covers this intrinsic.

// CHECK-LABEL: llvm.func @samplecmp_bias
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: llvm.call_intrinsic "llvm.spv.resource.samplecmpbias"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}})
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @samplecmp_bias(%coord : vector<2xf32>, %dref : f32, %bias : f32) -> f32 "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleDrefImplicitLod %4, %coord, %dref ["Bias"], %bias : !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32, f32 -> f32
    spirv.ReturnValue %5 : f32
  }
}

// -----

// Roadmap L52(b): `Bias|ConstOffset|MinLod` all together converts to
// `llvm.spv.resource.samplecmpbias.clamp`, threading every real operand
// through in SPIR-V's own fixed bit order (bias, offset, clamp) after the
// dref -- the depth-comparison counterpart of the non-`Dref` sibling
// pattern's `llvm.spv.resource.samplebias.clamp`.

// CHECK-LABEL: llvm.func @samplecmp_bias_const_offset_minlod
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: llvm.call_intrinsic "llvm.spv.resource.samplecmpbias.clamp"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}})
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @samplecmp_bias_const_offset_minlod(%coord : vector<2xf32>, %dref : f32, %bias : f32, %offset : vector<2xsi32>, %clamp : f32) -> f32 "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleDrefImplicitLod %4, %coord, %dref ["Bias|ConstOffset|MinLod"], %bias, %offset, %clamp : !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32, f32, vector<2xsi32>, f32 -> f32
    spirv.ReturnValue %5 : f32
  }
}
// -----

// Roadmap L31: `spirv.ImageSampleDrefExplicitLod` with a literal `Lod =
// 0.0` image operand (what `dxc` emits for
// `Texture2D<T>::SampleCmpLevelZero(sampler, coord, dref)`) converts to the
// `llvm.spv.resource.samplecmplevelzero` intrinsic, which has no LOD
// operand at all in its own signature (it implicitly always samples mip
// level zero) -- see `llvm/test/CodeGen/SPIRV/hlsl-resources/
// SampleCmpLevelZero.ll`.

// CHECK-LABEL: llvm.func @samplecmplevelzero
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: %[[OFFSET:.*]] = llvm.mlir.constant(dense<0> : vector<2xi32>) : vector<2xi32>
// CHECK: llvm.call_intrinsic "llvm.spv.resource.samplecmplevelzero"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %[[OFFSET]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @samplecmplevelzero(%coord : vector<2xf32>, %dref : f32) -> f32 "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %lod = spirv.Constant 0.0 : f32
    %5 = spirv.ImageSampleDrefExplicitLod %4, %coord, %dref ["Lod"], %lod : !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32, f32 -> f32
    spirv.ReturnValue %5 : f32
  }
}

// -----

// Roadmap L31: `spirv.ImageSampleDrefExplicitLod` with a literal `Lod =
// 0.0` image operand, combined with `ConstOffset`, still converts to
// `llvm.spv.resource.samplecmplevelzero`, threading the real offset
// through instead of the defaulted zero the unmodified case above uses.

// CHECK-LABEL: llvm.func @samplecmplevelzero_const_offset
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: llvm.call_intrinsic "llvm.spv.resource.samplecmplevelzero"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %[[OFFSET:.*]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @samplecmplevelzero_const_offset(%coord : vector<2xf32>, %dref : f32, %offset : vector<2xsi32>) -> f32 "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %lod = spirv.Constant 0.0 : f32
    %5 = spirv.ImageSampleDrefExplicitLod %4, %coord, %dref ["Lod|ConstOffset"], %lod, %offset : !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32, f32, vector<2xsi32> -> f32
    spirv.ReturnValue %5 : f32
  }
}

// -----

// Roadmap L31: `spirv.ImageQueryLod` converts to two
// `llvm.spv.resource.calculate.lod`/`.calculate.lod.unclamped` intrinsic
// calls (LLVM's SPIRV backend's own `OpImageQueryLod` selection runs the
// reverse direction, building one two-component result *from* these same
// two intrinsics -- see `llvm/test/CodeGen/SPIRV/hlsl-resources/
// CalculateLevelOfDetail.ll`), combined into one `vector<2xf32>` result via
// two `llvm.insertelement`s: lane 0 (clamped) from `calculate.lod`, lane 1
// (unclamped) from `calculate.lod.unclamped`.

// CHECK-LABEL: llvm.func @querylod
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: %[[CLAMPED:.*]] = llvm.call_intrinsic "llvm.spv.resource.calculate.lod"(%[[IMG]], %[[SAMP]], %{{.*}}) : {{.*}} -> f32
// CHECK: %[[UNCLAMPED:.*]] = llvm.call_intrinsic "llvm.spv.resource.calculate.lod.unclamped"(%[[IMG]], %[[SAMP]], %{{.*}}) : {{.*}} -> f32
// CHECK: %[[BASE:.*]] = llvm.mlir.poison : vector<2xf32>
// CHECK: %[[V0:.*]] = llvm.insertelement %[[CLAMPED]], %[[BASE]][%{{.*}} : i64] : vector<2xf32>
// CHECK: llvm.insertelement %[[UNCLAMPED]], %[[V0]][%{{.*}} : i64] : vector<2xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @querylod(%coord : vector<2xf32>) -> vector<2xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageQueryLod %4, %coord : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32> -> vector<2xf32>
    spirv.ReturnValue %5 : vector<2xf32>
  }
}
