// Roadmap L31: `spirv.ImageSampleDrefImplicitLod`/`ImageSampleDrefExplicitLod`
// image-operand combinations with no supported mapping must be rejected via
// a clean legalization failure rather than crashing. (Roadmap L66(c): a
// `Grad` image operand on `ImageSampleDrefExplicitLod` used to have no
// supported mapping and was rejected here too -- it is now legalized by
// `ImageSampleDrefGradPattern` instead, so that case moved to
// `spirv-to-llvm-sample-dref-and-query-lod.mlir`'s own positive coverage.)

// RUN: feme-opt --feme-convert-spirv-to-llvm --verify-diagnostics --split-input-file --mlir-very-unsafe-disable-verifier-on-parsing %s

// A nonzero, non-constant `Lod` operand has no supported mapping for a
// `Dref` explicit-LOD sample: `llvm.spv.resource.samplecmplevelzero` has no
// LOD operand at all in its own signature (it implicitly always samples
// mip level zero), so only a literal, compile-time-constant zero `Lod` can
// be represented; any other `Lod` value must be rejected rather than
// silently sampling the wrong mip level.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @samplecmp_lod_nonconstant_unsupported(%coord : vector<2xf32>, %dref : f32, %lod : f32) -> f32 "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    // expected-error@+1 {{failed to legalize operation 'spirv.ImageSampleDrefExplicitLod' that was explicitly marked illegal}}
    %5 = spirv.ImageSampleDrefExplicitLod %4, %coord, %dref ["Lod"], %lod : !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32, f32 -> f32
    spirv.ReturnValue %5 : f32
  }
}

// -----

// A literal, but nonzero, constant `Lod` operand also has no supported
// mapping for a `Dref` explicit-LOD sample, for the same reason.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @samplecmp_lod_nonzero_unsupported(%coord : vector<2xf32>, %dref : f32) -> f32 "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %lod = spirv.Constant 1.0 : f32
    // expected-error@+1 {{failed to legalize operation 'spirv.ImageSampleDrefExplicitLod' that was explicitly marked illegal}}
    %5 = spirv.ImageSampleDrefExplicitLod %4, %coord, %dref ["Lod"], %lod : !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32, f32 -> f32
    spirv.ReturnValue %5 : f32
  }
}
