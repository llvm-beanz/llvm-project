// RUN: feme-opt --mlir-very-unsafe-disable-verifier-on-parsing --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that image accesses become the `llvm.spv.resource.*` intrinsics
// LLVM's SPIRV backend selects `OpImageRead`/`OpImageWrite`/`OpImageQuerySize`
// from -- the last of the SPIR-V constructs MLIR's own conversion has no
// pattern for at all (see the "Known gap" note in the SPIR-V section of
// feme/docs/Design.md).
//
// `--mlir-very-unsafe-disable-verifier-on-parsing` is required for this
// whole file because `read_write_zero_sign_extend`/`sample_zero_extend`
// below use the `SignExtend`/`ZeroExtend` image operands, which upstream
// `mlir/lib/Dialect/SPIRV/IR/ImageOps.cpp`'s `verifyImageOperands`
// unconditionally rejects via a hard `assert` (a "TODO: Add the
// validation rules for the following Image Operands" left unimplemented,
// not a real structural problem -- see feme/test/Import/SPIRV/
// spirv-import-skip-verify.test for the same upstream gap hit by a
// different operand set on the *import* path); disabling verification on
// parse does not weaken this file's own test oracle, since every case
// here is still checked by its `CHECK` lines matching the conversion
// pass's actual output, and the pass itself still runs its normal
// legality checks (this flag only skips the a-priori structural
// verification MLIR would otherwise run against the parsed input).

// CHECK-LABEL: llvm.func @read_write
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[READ_PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}}) : (!llvm.target<"spirv.Image", f32, 5, 0, 0, 0, 2, 1>, i32) -> !llvm.ptr
// CHECK: %[[TEXEL:.*]] = llvm.load %[[READ_PTR]] : !llvm.ptr -> vector<4xf32>
// CHECK: %[[WRITE_PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: llvm.store %[[TEXEL]], %[[WRITE_PTR]] : vector<4xf32>, !llvm.ptr
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @buf bind(0, 1) : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
  spirv.func @read_write(%coord : i32) -> () "None" {
    %0 = spirv.mlir.addressof @buf : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>
    %2 = spirv.ImageRead %1, %coord : !spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, i32 -> vector<4xf32>
    spirv.ImageWrite %1, %coord, %2 : !spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, i32, vector<4xf32>
    spirv.Return
  }
}

// -----

// A size query picks the intrinsic returning as many dimensions as it asks
// for.

// CHECK-LABEL: llvm.func @query_size
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: llvm.call_intrinsic "llvm.spv.resource.getdimensions.x"(%[[HANDLE]]) : (!llvm.target<"spirv.Image", f32, 5, 0, 0, 0, 2, 1>) -> i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @buf bind(0, 1) : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
  spirv.func @query_size() -> i32 "None" {
    %0 = spirv.mlir.addressof @buf : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>
    %2 = spirv.ImageQuerySize %1 : !spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f> -> i32
    spirv.ReturnValue %2 : i32
  }
}

// -----

// A `dxc`-compiled shader spells "no image operand modifiers" as an
// explicit `#spirv.image_operands<None>` attribute rather than omitting the
// (optional) attribute entirely; a presence check alone would reject a real
// access rather than only ones with actual modifiers (`Lod`, `Bias`, ...).

// CHECK-LABEL: llvm.func @read_write_none_operands
// CHECK: llvm.call_intrinsic "llvm.spv.resource.getpointer"
// CHECK: llvm.load
// CHECK: llvm.call_intrinsic "llvm.spv.resource.getpointer"
// CHECK: llvm.store
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @buf bind(0, 1) : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
  spirv.func @read_write_none_operands(%coord : i32) -> () "None" {
    %0 = spirv.mlir.addressof @buf : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>
    %2 = "spirv.ImageRead"(%1, %coord) <{image_operands = #spirv.image_operands<None>}> : (!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, i32) -> vector<4xf32>
    "spirv.ImageWrite"(%1, %coord, %2) <{image_operands = #spirv.image_operands<None>}> : (!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, i32, vector<4xf32>) -> ()
    spirv.Return
  }
}

// -----

// SPIR-V 1.6's `Nontemporal` image-operand bit is a cache hint with no
// correctness effect; this converter has no caching model to honor it
// with, so it is accepted and discarded rather than rejected like an
// unmodeled modifier (roadmap E17).

// CHECK-LABEL: llvm.func @read_write_nontemporal
// CHECK: llvm.call_intrinsic "llvm.spv.resource.getpointer"
// CHECK: llvm.load
// CHECK: llvm.call_intrinsic "llvm.spv.resource.getpointer"
// CHECK: llvm.store
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @buf bind(0, 1) : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
  spirv.func @read_write_nontemporal(%coord : i32) -> () "None" {
    %0 = spirv.mlir.addressof @buf : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>
    %2 = "spirv.ImageRead"(%1, %coord) <{image_operands = #spirv.image_operands<Nontemporal>}> : (!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, i32) -> vector<4xf32>
    "spirv.ImageWrite"(%1, %coord, %2) <{image_operands = #spirv.image_operands<Nontemporal>}> : (!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, i32, vector<4xf32>) -> ()
    spirv.Return
  }
}

// CHECK-LABEL: llvm.func @query_size_2d
// CHECK: llvm.call_intrinsic "llvm.spv.resource.getdimensions.xy"({{.*}}) : (!llvm.target<"spirv.Image", f32, 1, 0, 0, 0, 2, 1>) -> vector<2xi32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @tex bind(0, 2) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
  spirv.func @query_size_2d() -> vector<2xi32> "None" {
    %0 = spirv.mlir.addressof @tex : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>
    %2 = spirv.ImageQuerySize %1 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f> -> vector<2xi32>
    spirv.ReturnValue %2 : vector<2xi32>
  }
}

// -----

// Roadmap H71: any integer-format image (here `R32ui`, matching what
// `glslangValidator --target-env vulkan1.2` emits for `imageLoad`/
// `imageStore` on a `r32ui` image) gets an explicit `SignExtend`/
// `ZeroExtend` image operand, since SPIR-V's `OpTypeImage` "Sampled Type"
// is always the full-width scalar type regardless of the image's real,
// possibly-narrower storage format -- this operand only tells a real
// GPU's texture unit how to widen a narrower on-disk texel into that
// type, which this converter does not need (the equivalent conversion is
// driven by the resource's real `VkFormat` elsewhere, in the CPU
// executor's own image fixture); like `Nontemporal` above, it is accepted
// and discarded rather than rejected like an unmodeled modifier (see
// `DiscardedImageOperandBits` in SPIRVToLLVMPatterns.cpp).

// CHECK-LABEL: llvm.func @read_write_zero_sign_extend
// CHECK: llvm.call_intrinsic "llvm.spv.resource.getpointer"
// CHECK: llvm.load
// CHECK: llvm.call_intrinsic "llvm.spv.resource.getpointer"
// CHECK: llvm.store
spirv.module Logical GLSL450 requires #spirv.vce<v1.5, [Shader], []> {
  spirv.GlobalVariable @buf bind(0, 1) : !spirv.ptr<!spirv.image<i32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, R32ui>, UniformConstant>
  spirv.func @read_write_zero_sign_extend(%coord : i32) -> () "None" {
    %0 = spirv.mlir.addressof @buf : !spirv.ptr<!spirv.image<i32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, R32ui>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<i32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, R32ui>
    %2 = "spirv.ImageRead"(%1, %coord) <{image_operands = #spirv.image_operands<ZeroExtend>}> : (!spirv.image<i32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, R32ui>, i32) -> vector<4xi32>
    "spirv.ImageWrite"(%1, %coord, %2) <{image_operands = #spirv.image_operands<SignExtend>}> : (!spirv.image<i32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, R32ui>, i32, vector<4xi32>) -> ()
    spirv.Return
  }
}

// -----

// The same `ZeroExtend` bit shows up on `ImageSampleExplicitLod` for an
// integer-format sampled image (e.g. `usampler2D` in GLSL); it is combined
// with `Lod` here exactly as glslang emits it, to check the discard logic
// also applies once another, still-meaningful image operand is present in
// the same bitmask.

// CHECK-LABEL: llvm.func @sample_zero_extend
// CHECK: llvm.call_intrinsic "llvm.spv.resource.samplelevel"
spirv.module Logical GLSL450 requires #spirv.vce<v1.5, [Shader], []> {
  spirv.GlobalVariable @tex bind(0, 0) : !spirv.ptr<!spirv.sampled_image<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, UniformConstant>
  spirv.func @sample_zero_extend(%coord : vector<2xf32>, %lod : f32) -> vector<4xi32> "None" {
    %0 = spirv.mlir.addressof @tex : !spirv.ptr<!spirv.sampled_image<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.sampled_image<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %2 = "spirv.ImageSampleExplicitLod"(%1, %coord, %lod) <{image_operands = #spirv.image_operands<Lod|ZeroExtend>}> : (!spirv.sampled_image<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32) -> vector<4xi32>
    spirv.ReturnValue %2 : vector<4xi32>
  }
}
