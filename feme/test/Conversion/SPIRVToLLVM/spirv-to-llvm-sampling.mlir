// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file --mlir-very-unsafe-disable-verifier-on-parsing %s | FileCheck %s

// Checks that sampling an image -- `spirv.SampledImage` combining an image
// and sampler handle, followed by `spirv.ImageSampleImplicitLod` -- becomes
// the `llvm.spv.resource.sample` call LLVM's SPIRV backend selects
// `OpSampledImage`+`OpImageSampleImplicitLod` from (see
// `llvm/test/CodeGen/SPIRV/hlsl-resources/Sample.ll`): the two handles are
// carried as a struct through the dialect conversion (there is no combined
// handle value at the LLVM IR level, unlike MLIR's own conversion, which
// targets the SPIR-V runner) and unpacked again where the intrinsic needs
// them as separate arguments.
//
// Roadmap L22: `--mlir-very-unsafe-disable-verifier-on-parsing` is needed
// for this file's own `ConstOffset`/`Bias|ConstOffset`/`MinLod`-involving
// cases below -- `mlir::verifyImageOperands` (`ImageOps.cpp`) has a real,
// pre-existing upstream `assert` for both `ConstOffset` and `MinLod`
// ("TODO: Add the validation rules for the following Image Operands"),
// never implemented since no in-tree user needed them. `feme`'s own real
// import path (`SPIRVImporter.cpp`) builds these ops without ever invoking
// `mlir::verify()` at all (confirmed by inspection -- no call site
// anywhere in this ICD's own pipeline), which is how a real `ConstOffset`/
// `MinLod` sample reaches legalization at all rather than crashing; this
// flag makes the lit test's own textual-assembly parsing path behave the
// same way the real production path already does, rather than papering
// over a bug this pattern needs to handle.

// CHECK-LABEL: llvm.func @sample
// CHECK: %[[IMG_HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.Image"
// CHECK: %[[SAMP_HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.Sampler">
// CHECK: %[[PAIR0:.*]] = llvm.mlir.poison : !llvm.struct<(target<"spirv.Image"{{.*}}>, target<"spirv.Sampler">)>
// CHECK: %[[PAIR1:.*]] = llvm.insertvalue %[[IMG_HANDLE]], %[[PAIR0]][0]
// CHECK: %[[PAIR2:.*]] = llvm.insertvalue %[[SAMP_HANDLE]], %[[PAIR1]][1]
// CHECK: %[[IMG:.*]] = llvm.extractvalue %[[PAIR2]][0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %[[PAIR2]][1]
// CHECK: %[[OFFSET:.*]] = llvm.mlir.constant(dense<0> : vector<2xi32>) : vector<2xi32>
// CHECK: llvm.call_intrinsic "llvm.spv.resource.sample"(%[[IMG]], %[[SAMP]], %{{.*}}, %[[OFFSET]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @sample(%coord : vector<2xf32>) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleImplicitLod %4, %coord : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32> -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// `spirv.ImageFetch` -- a direct texel fetch from a (non-`RWTexture`)
// `Texture` resource, as opposed to `spirv.ImageRead`'s load from a UAV --
// converts exactly like `spirv.ImageRead` does: LLVM's SPIRV backend itself
// picks `OpImageFetch` vs `OpImageRead` from the handle's underlying image
// type (whether its `Sampled` operand is 1), not from which intrinsic
// produced the load (see `generateImageReadOrFetch` in
// `llvm/lib/Target/SPIRV/SPIRVInstructionSelector.cpp`), so no separate
// intrinsic is needed for it.

// CHECK-LABEL: llvm.func @fetch
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: llvm.load %[[PTR]] : !llvm.ptr -> vector<4xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @tex bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.func @fetch(%coord : vector<2xsi32>) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @tex : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.ImageFetch %1, %coord : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, vector<2xsi32> -> vector<4xf32>
    spirv.ReturnValue %2 : vector<4xf32>
  }
}

// -----

// `spirv.ImageSampleExplicitLod` with a lone `Lod` image operand converts to
// `llvm.spv.resource.samplelevel`, threading the explicit LOD value through
// instead of defaulting it the way `ImageSampleImplicitLodPattern` does
// (roadmap R30).

// CHECK-LABEL: llvm.func @sample_level
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: %[[OFFSET:.*]] = llvm.mlir.constant(dense<0> : vector<2xi32>) : vector<2xi32>
// CHECK: llvm.call_intrinsic "llvm.spv.resource.samplelevel"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %[[OFFSET]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @sample_level(%coord : vector<2xf32>, %lod : f32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleExplicitLod %4, %coord ["Lod"], %lod : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32 -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// SPIR-V 1.6's `Nontemporal` image-operand bit, combined with `Lod`, still
// converts to `llvm.spv.resource.samplelevel` the same way (roadmap E17,
// see the `@fetch_level_nontemporal` case below for the fetch analogue).

// CHECK-LABEL: llvm.func @sample_level_nontemporal
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: %[[OFFSET:.*]] = llvm.mlir.constant(dense<0> : vector<2xi32>) : vector<2xi32>
// CHECK: llvm.call_intrinsic "llvm.spv.resource.samplelevel"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %[[OFFSET]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @sample_level_nontemporal(%coord : vector<2xf32>, %lod : f32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleExplicitLod %4, %coord ["Lod|Nontemporal"], %lod : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32 -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// `spirv.ImageFetch` with a lone `Lod` image operand -- what `dxc` always
// emits for `Texture2D<T>::Load`, even a literal 0 mip -- converts to
// `llvm.spv.resource.load.level`, threading the explicit mip level through
// instead of the plain `llvm.load` the unmodified `@fetch` case above uses.

// CHECK-LABEL: llvm.func @fetch_level
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[OFFSET:.*]] = llvm.mlir.constant(dense<0> : vector<2xi32>) : vector<2xi32>
// CHECK: llvm.call_intrinsic "llvm.spv.resource.load.level"(%[[HANDLE]], %{{.*}}, %{{.*}}, %[[OFFSET]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @tex bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.func @fetch_level(%coord : vector<2xsi32>, %lod : si32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @tex : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.ImageFetch %1, %coord ["Lod"], %lod : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, vector<2xsi32>, si32 -> vector<4xf32>
    spirv.ReturnValue %2 : vector<4xf32>
  }
}

// -----

// SPIR-V 1.6's `Nontemporal` image-operand bit, combined with `Lod`, still
// converts to `llvm.spv.resource.load.level` -- the cache hint has no
// correctness effect and this converter has no caching model to honor it
// with, so it is accepted and discarded rather than rejected the way any
// other unmodeled modifier bit would be (roadmap E17).

// CHECK-LABEL: llvm.func @fetch_level_nontemporal
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[OFFSET:.*]] = llvm.mlir.constant(dense<0> : vector<2xi32>) : vector<2xi32>
// CHECK: llvm.call_intrinsic "llvm.spv.resource.load.level"(%[[HANDLE]], %{{.*}}, %{{.*}}, %[[OFFSET]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @tex bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.func @fetch_level_nontemporal(%coord : vector<2xsi32>, %lod : si32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @tex : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.ImageFetch %1, %coord ["Lod|Nontemporal"], %lod : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, vector<2xsi32>, si32 -> vector<4xf32>
    spirv.ReturnValue %2 : vector<4xf32>
  }
}

// -----

// Roadmap L22: `spirv.ImageSampleImplicitLod` with a lone `ConstOffset`
// image operand -- what `dxc` emits for `Texture2D<T>::Sample(sampler,
// coord, offset)` -- converts to the same `llvm.spv.resource.sample`
// intrinsic as the unmodified case, but threading the real offset operand
// through instead of hardcoding zero; LLVM's SPIRV backend itself decides
// whether to actually emit the `ConstOffset` image operand, folding it away
// only when the constant is all-zero (see
// `llvm/test/CodeGen/SPIRV/hlsl-resources/SampleBias.ll`'s own `res0`
// case).

// CHECK-LABEL: llvm.func @sample_const_offset
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: llvm.call_intrinsic "llvm.spv.resource.sample"(%[[IMG]], %[[SAMP]], %{{.*}}, %[[OFFSET:.*]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @sample_const_offset(%coord : vector<2xf32>, %offset : vector<2xsi32>) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleImplicitLod %4, %coord ["ConstOffset"], %offset : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, vector<2xsi32> -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// Roadmap L22: `spirv.ImageSampleImplicitLod` with a lone `Bias` image
// operand -- what `dxc` emits for `Texture2D<T>::SampleBias` -- converts to
// the `llvm.spv.resource.samplebias` intrinsic, threading the bias value
// through with a defaulted zero offset (see
// `llvm/test/CodeGen/SPIRV/hlsl-resources/SampleBias.ll`).

// CHECK-LABEL: llvm.func @sample_bias
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: %[[OFFSET:.*]] = llvm.mlir.constant(dense<0> : vector<2xi32>) : vector<2xi32>
// CHECK: llvm.call_intrinsic "llvm.spv.resource.samplebias"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %[[OFFSET]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @sample_bias(%coord : vector<2xf32>, %bias : f32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleImplicitLod %4, %coord ["Bias"], %bias : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32 -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// Roadmap L22: `spirv.ImageSampleImplicitLod` with `Bias|ConstOffset`
// together (what `dxc` emits for `Texture2D<T>::SampleBias(sampler, coord,
// bias, offset)`) converts to `llvm.spv.resource.samplebias` with the real
// offset threaded through instead of the defaulted zero the lone-`Bias`
// case above uses.

// CHECK-LABEL: llvm.func @sample_bias_const_offset
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: llvm.call_intrinsic "llvm.spv.resource.samplebias"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %[[OFFSET:.*]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @sample_bias_const_offset(%coord : vector<2xf32>, %bias : f32, %offset : vector<2xsi32>) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleImplicitLod %4, %coord ["Bias|ConstOffset"], %bias, %offset : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32, vector<2xsi32> -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// Roadmap L22: `spirv.ImageSampleImplicitLod` with a lone `MinLod` image
// operand (a min-LOD clamp -- what `dxc` emits for `Texture2D<T>::Sample`'s
// own optional trailing `clamp` argument, `Sample(sampler, coord, offset,
// clamp)`, whenever its `offset` argument happens to be all-zero, since the
// backend folds an all-zero `ConstOffset` away independently of whether
// `MinLod` is also present -- see `llvm/test/CodeGen/SPIRV/hlsl-resources/
// Sample.ll`'s own `res2` case) converts to `llvm.spv.resource.sample.clamp`
// with a defaulted zero offset.

// CHECK-LABEL: llvm.func @sample_minlod
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: %[[OFFSET:.*]] = llvm.mlir.constant(dense<0> : vector<2xi32>) : vector<2xi32>
// CHECK: llvm.call_intrinsic "llvm.spv.resource.sample.clamp"(%[[IMG]], %[[SAMP]], %{{.*}}, %[[OFFSET]], %{{.*}})
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @sample_minlod(%coord : vector<2xf32>, %clamp : f32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleImplicitLod %4, %coord ["MinLod"], %clamp : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32 -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// Roadmap L22: `spirv.ImageSampleImplicitLod` with `ConstOffset|MinLod`
// together -- what `dxc` emits for `Texture2D<T>::Sample(sampler, coord,
// offset, clamp)` when both `offset` and `clamp` are non-zero -- converts
// to `llvm.spv.resource.sample.clamp` with the real offset threaded
// through instead of the defaulted zero the lone-`MinLod` case above uses.
// This is the exact shape a real check-hlsl-feme-vk run
// (Feature/Textures/Sample.test, Feature/Vk.SampledTextures/
// Vk.SampledTexture2D/Vk.SampledTexture2D.Sample.test.yaml) failed
// `vkCreateGraphicsPipelines` on before this fix, with `"failed to
// legalize operation 'spirv.ImageSampleImplicitLod' that was explicitly
// marked illegal"` for an `image_operands = #spirv.image_operands<
// ConstOffset|MinLod>` op.

// CHECK-LABEL: llvm.func @sample_const_offset_minlod
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: llvm.call_intrinsic "llvm.spv.resource.sample.clamp"(%[[IMG]], %[[SAMP]], %{{.*}}, %[[OFFSET:.*]], %{{.*}})
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @sample_const_offset_minlod(%coord : vector<2xf32>, %offset : vector<2xsi32>, %clamp : f32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleImplicitLod %4, %coord ["ConstOffset|MinLod"], %offset, %clamp : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, vector<2xsi32>, f32 -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// Roadmap L22: `spirv.ImageSampleImplicitLod` with `Bias|ConstOffset|MinLod`
// together -- what `dxc` emits for `Texture2D<T>::SampleBias(sampler,
// coord, bias, offset, clamp)` -- converts to
// `llvm.spv.resource.samplebias.clamp`, the last of the eight
// `Bias`/`ConstOffset`/`MinLod` combinations this pattern supports.

// CHECK-LABEL: llvm.func @sample_bias_const_offset_minlod
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: llvm.call_intrinsic "llvm.spv.resource.samplebias.clamp"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %[[OFFSET:.*]], %{{.*}})
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @sample_bias_const_offset_minlod(%coord : vector<2xf32>, %bias : f32, %offset : vector<2xsi32>, %clamp : f32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleImplicitLod %4, %coord ["Bias|ConstOffset|MinLod"], %bias, %offset, %clamp : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32, vector<2xsi32>, f32 -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// Roadmap L59: `spirv.ImageSampleExplicitLod` with a lone `Grad` image
// operand (GLSL's `textureGrad()`, HLSL's `Texture2D::SampleGrad`) --
// `ImageSampleExplicitLodPattern`'s own exact `Lod`-only match above fails
// this call, so the greedy pattern rewriter falls through to
// `ImageSampleGradPattern` instead, converting it to the
// `llvm.spv.resource.samplegrad` intrinsic with the two real gradient
// vectors (`dPdx`, `dPdy`) threaded through, mirroring `@sample_bias`'s own
// single-`Bias`-operand shape above but for a pair of vector operands
// instead of one scalar.

// CHECK-LABEL: llvm.func @sample_grad
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: %[[OFFSET:.*]] = llvm.mlir.constant(dense<0> : vector<2xi32>) : vector<2xi32>
// CHECK: llvm.call_intrinsic "llvm.spv.resource.samplegrad"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %{{.*}}, %[[OFFSET]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @sample_grad(%coord : vector<2xf32>, %dpdx : vector<2xf32>, %dpdy : vector<2xf32>) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleExplicitLod %4, %coord ["Grad"], %dpdx, %dpdy : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, vector<2xf32>, vector<2xf32> -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// Roadmap L64: an *arrayed* `Grad` sample. SPIR-V gives a `Grad` derivative
// one component per image dimension *not counting* the array layer, so this
// `Arrayed` 2D image pairs a 3-component `(U, V, Layer)` coordinate with
// 2-component derivatives. This conversion forwards all three operands
// unchanged whatever their widths -- pinned here because the downstream
// `feme-cpu-lower-spirv-resources` width check used to (wrongly) require the
// derivatives to be as wide as the coordinate, rejecting exactly this shape.

// CHECK-LABEL: llvm.func @sample_grad_arrayed
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: llvm.call_intrinsic "llvm.spv.resource.samplegrad"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}})
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, Arrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @sample_grad_arrayed(%coord : vector<3xf32>, %dpdx : vector<2xf32>, %dpdy : vector<2xf32>) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, Arrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, Arrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, Arrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, Arrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleExplicitLod %4, %coord ["Grad"], %dpdx, %dpdy : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, Arrayed, SingleSampled, NeedSampler, Unknown>>, vector<3xf32>, vector<2xf32>, vector<2xf32> -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// Roadmap L59: `spirv.ImageSampleExplicitLod` with `Grad|ConstOffset`
// together (GLSL's `textureGradOffset()`) converts to
// `llvm.spv.resource.samplegrad` with the real, possibly-nonzero offset
// threaded through instead of the always-zero default `@sample_grad` above
// gets.

// CHECK-LABEL: llvm.func @sample_grad_const_offset
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: llvm.call_intrinsic "llvm.spv.resource.samplegrad"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %{{.*}}, %[[OFFSET:.*]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @sample_grad_const_offset(%coord : vector<2xf32>, %dpdx : vector<2xf32>, %dpdy : vector<2xf32>, %offset : vector<2xsi32>) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleExplicitLod %4, %coord ["Grad|ConstOffset"], %dpdx, %dpdy, %offset : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, vector<2xf32>, vector<2xf32>, vector<2xsi32> -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// Roadmap L59: `spirv.ImageSampleExplicitLod` with `Grad|MinLod` together
// (HLSL's `Texture2D::SampleGrad`'s trailing `clamp` overload) converts to
// `llvm.spv.resource.samplegrad.clamp`, with the real clamp value threaded
// through as the intrinsic's own trailing operand -- mirroring
// `@sample_bias_const_offset_minlod`'s own `.clamp` variant above.

// CHECK-LABEL: llvm.func @sample_grad_minlod
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: %[[OFFSET:.*]] = llvm.mlir.constant(dense<0> : vector<2xi32>) : vector<2xi32>
// CHECK: llvm.call_intrinsic "llvm.spv.resource.samplegrad.clamp"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %{{.*}}, %[[OFFSET]], %{{.*}})
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @sample_grad_minlod(%coord : vector<2xf32>, %dpdx : vector<2xf32>, %dpdy : vector<2xf32>, %clamp : f32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleExplicitLod %4, %coord ["Grad|MinLod"], %dpdx, %dpdy, %clamp : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, vector<2xf32>, vector<2xf32>, f32 -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// Roadmap L59: `spirv.ImageSampleExplicitLod` with `Grad|ConstOffset|MinLod`
// together converts to `llvm.spv.resource.samplegrad.clamp` with both the
// real offset and clamp threaded through, the last of the four
// `ConstOffset`/`MinLod` combinations this pattern supports alongside its
// own mandatory `Grad` operand.

// CHECK-LABEL: llvm.func @sample_grad_const_offset_minlod
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: llvm.call_intrinsic "llvm.spv.resource.samplegrad.clamp"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %{{.*}}, %[[OFFSET:.*]], %{{.*}})
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @sample_grad_const_offset_minlod(%coord : vector<2xf32>, %dpdx : vector<2xf32>, %dpdy : vector<2xf32>, %offset : vector<2xsi32>, %clamp : f32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleExplicitLod %4, %coord ["Grad|ConstOffset|MinLod"], %dpdx, %dpdy, %offset, %clamp : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, vector<2xf32>, vector<2xf32>, vector<2xsi32>, f32 -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}
