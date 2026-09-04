// This test needs `--mlir-very-unsafe-disable-verifier-on-parsing` for the
// same reason spirv-to-llvm-sampling.mlir's own `sample_const_offset` case
// does (see that file's own comment): `Offset` is one of
// `mlir/lib/Dialect/SPIRV/IR/ImageOps.cpp`'s own `verifyImageOperands`
// `noSupportOperands` (an explicit upstream MLIR TODO, not yet validated at
// all), which asserts rather than emitting a clean diagnostic when the
// textual-assembly parser's own post-construction `verify()` call reaches
// it -- a path FeMe's own real production import (`SPIRVImporter.cpp`)
// never takes, since it never calls `mlir::verify()` at all.

// RUN: feme-opt --feme-convert-spirv-to-llvm --verify-diagnostics --split-input-file --mlir-very-unsafe-disable-verifier-on-parsing %s

// The discarded `Nontemporal` cache-hint bit (roadmap E17) must not mask an
// actual, still-unsupported modifier combined with it: `Offset` (a
// *dynamic*, non-constant per-texel offset -- distinct from `ConstOffset`,
// which roadmap L22 gave a pattern) has no pattern here (see the "Known
// gap" note in the SPIR-V section of feme/docs/Design.md), so a
// `Bias|Offset|Nontemporal` image operand mask must still be rejected,
// even though `Bias` alone (or `Bias|ConstOffset`, `Bias|ConstOffset|
// MinLod`, ...) is supported. (`Bias`/`ConstOffset`/`MinLod` themselves all
// gained a pattern under roadmap L22 -- see spirv-to-llvm-sampling.mlir's
// own `sample_bias`/`sample_bias_const_offset`/`sample_minlod`/
// `sample_bias_const_offset_minlod` cases -- so this test no longer uses
// any of them as its "still-unsupported" example modifier.)

spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @sample_bias_offset_nontemporal(%coord : vector<2xf32>, %bias : f32, %offset : vector<2xsi32>) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    // expected-error@+1 {{failed to legalize operation 'spirv.ImageSampleImplicitLod' that was explicitly marked illegal}}
    %5 = spirv.ImageSampleImplicitLod %4, %coord ["Bias|Offset|Nontemporal"], %bias, %offset : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32, vector<2xsi32> -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}
