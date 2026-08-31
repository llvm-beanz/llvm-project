// RUN: feme-opt --feme-convert-spirv-to-llvm --verify-diagnostics --split-input-file %s

// The discarded `Nontemporal` cache-hint bit (roadmap E17) must not mask an
// actual, still-unsupported modifier combined with it: `Bias` has no
// pattern here (see the "Known gap" note in the SPIR-V section of
// feme/docs/Design.md), so a `Bias|Nontemporal` image operand mask must
// still be rejected exactly like a lone `Bias` would be.

spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @sample_bias_nontemporal(%coord : vector<2xf32>, %bias : f32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    // expected-error@+1 {{failed to legalize operation 'spirv.ImageSampleImplicitLod' that was explicitly marked illegal}}
    %5 = spirv.ImageSampleImplicitLod %4, %coord ["Bias|Nontemporal"], %bias : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32 -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// Roadmap H19g only widens a plain (non-arrayed) `Dim::2D` multisampled
// storage image's own `Sample` image operand -- an *arrayed* multisampled
// storage image still needs a 4-component coordinate no call vocabulary or
// runtime helper implements yet (see Roadmap.md's H19g breakdown), so its
// own `Sample` operand must still be rejected.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, Arrayed, MultiSampled, NoSampler, Rgba32f>, UniformConstant>
  spirv.func @read_arrayed_ms(%coord : vector<3xi32>, %sample : si32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, Arrayed, MultiSampled, NoSampler, Rgba32f>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, Arrayed, MultiSampled, NoSampler, Rgba32f>
    // expected-error@+1 {{failed to legalize operation 'spirv.ImageRead' that was explicitly marked illegal}}
    %2 = spirv.ImageRead %1, %coord ["Sample"], %sample : !spirv.image<f32, Dim2D, NoDepth, Arrayed, MultiSampled, NoSampler, Rgba32f>, vector<3xi32>, si32 -> vector<4xf32>
    spirv.ReturnValue %2 : vector<4xf32>
  }
}

