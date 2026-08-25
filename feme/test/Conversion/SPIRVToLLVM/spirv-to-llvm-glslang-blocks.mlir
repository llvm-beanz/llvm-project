// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks the shapes glslang itself emits for a storage/uniform buffer
// block, as opposed to FeMe's own upstream HLSL resource representation
// (see spirv-to-llvm-storage-buffer.mlir/spirv-to-llvm-uniform-buffer.mlir):
// glslang never wraps a block in an extra single-member struct, so its own
// `Block`/`BufferBlock`-decorated struct is free to declare more than one
// member directly.

// A pre-SPIR-V-1.3 SSBO: `Uniform` storage class, `BufferBlock` decoration,
// rather than `StorageBuffer`/`Block`. Otherwise identical to the ordinary
// single-member storage-buffer wrapper case.

// CHECK-LABEL: llvm.func @legacy_ssbo
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.array<0 x f32>, 2, 1>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: llvm.load %[[PTR]] : !llvm.ptr<12> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @out bind(0, 1) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0]), BufferBlock>, Uniform>
  spirv.func @legacy_ssbo(%idx : i32) -> f32 "None" {
    %0 = spirv.mlir.addressof @out : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0]), BufferBlock>, Uniform>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0]), BufferBlock>, Uniform>, i32, i32 -> !spirv.ptr<f32, Uniform>
    %v = spirv.Load "Uniform" %ac : f32
    spirv.ReturnValue %v : f32
  }
}

// -----

// A `Block`-decorated storage buffer struct with more than one member --
// a fixed header field alongside its trailing runtime array, the shape a
// plain GLSL `buffer B { uint count; float data[]; };` compiles to -- has
// no separate wrapper, so a header field access has only a single index,
// and the array's own member index (1) replaces the wrapper case's always-0
// leading one.

// CHECK-LABEL: llvm.func @header_field
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<(i32, array<0 x f32>)>, 12, 1>
// CHECK: %[[FIELD:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: llvm.load %[[FIELD]] : !llvm.ptr<11> -> i32

// CHECK-LABEL: llvm.func @array_element
// CHECK: %[[HANDLE2:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<(i32, array<0 x f32>)>, 12, 1>
// CHECK: %[[MEMBER:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE2]], %{{.*}})
// CHECK: %[[ELEM:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, %{{.*}}]
// CHECK: llvm.load %[[ELEM]] : !llvm.ptr<11> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @b bind(0, 2) : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.rtarray<f32, stride=4> [4]), Block>, StorageBuffer>
  spirv.func @header_field() -> i32 "None" {
    %0 = spirv.mlir.addressof @b : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.rtarray<f32, stride=4> [4]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0] : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.rtarray<f32, stride=4> [4]), Block>, StorageBuffer>, i32 -> !spirv.ptr<i32, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : i32
    spirv.ReturnValue %v : i32
  }
  spirv.func @array_element(%idx : i32) -> f32 "None" {
    %0 = spirv.mlir.addressof @b : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.rtarray<f32, stride=4> [4]), Block>, StorageBuffer>
    %c1 = spirv.Constant 1 : i32
    %ac = spirv.AccessChain %0[%c1, %idx] : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.rtarray<f32, stride=4> [4]), Block>, StorageBuffer>, i32, i32 -> !spirv.ptr<f32, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : f32
    spirv.ReturnValue %v : f32
  }
}

// -----

// A `Block`-decorated *uniform* struct with more than one member declared
// directly -- the shape a plain GLSL `uniform UBO { vec4 a; float b; };`
// compiles to, with no wrapper struct either.

// CHECK-LABEL: llvm.func @read_b
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<(vector<4xf32>, f32)>, 2, 0>
// CHECK: %[[FIELD:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: llvm.load %[[FIELD]] : !llvm.ptr<12> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ubo bind(0, 3) : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], f32 [16]), Block>, Uniform>
  spirv.func @read_b() -> f32 "None" {
    %0 = spirv.mlir.addressof @ubo : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], f32 [16]), Block>, Uniform>
    %c1 = spirv.Constant 1 : i32
    %ac = spirv.AccessChain %0[%c1] : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], f32 [16]), Block>, Uniform>, i32 -> !spirv.ptr<f32, Uniform>
    %v = spirv.Load "Uniform" %ac : f32
    spirv.ReturnValue %v : f32
  }
}

// -----

// A sized-array member (rather than a storage buffer's dynamically-sized
// trailing one), the shape a plain GLSL `uniform UBO { float data[4]; };`
// compiles to: a single member, so it is recognized as FeMe's own wrapper
// shape (roadmap F12a) exactly the way a storage buffer's sole runtime-
// array member already is, dynamically indexed directly through
// `llvm.spv.resource.getpointer` rather than through a `getelementptr` --
// which, unlike a storage buffer's own runtime array, cannot always
// reproduce a std140 array's own stride (see
// `feme::spirv::convertUniformArrayContent`'s comment) -- with that
// stride carried as the handle's own third integer parameter instead.

// CHECK-LABEL: llvm.func @read_element
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.array<0 x f32>, 2, 0, 4>
// CHECK: %[[ELEM:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: llvm.load %[[ELEM]] : !llvm.ptr<12> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ubo bind(0, 4) : !spirv.ptr<!spirv.struct<(!spirv.array<4 x f32, stride=4> [0]), Block>, Uniform>
  spirv.func @read_element(%idx : i32) -> f32 "None" {
    %0 = spirv.mlir.addressof @ubo : !spirv.ptr<!spirv.struct<(!spirv.array<4 x f32, stride=4> [0]), Block>, Uniform>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.array<4 x f32, stride=4> [0]), Block>, Uniform>, i32, i32 -> !spirv.ptr<f32, Uniform>
    %v = spirv.Load "Uniform" %ac : f32
    spirv.ReturnValue %v : f32
  }
}

// -----

// A cbuffer/UBO whose FeMe wrapper's sole member (the block's own field
// struct) itself has a sized-array field, and that field is indexed
// further -- caught a real bug during a Vulkan-CTS run
// (dEQP-VK.ubo.single_struct.per_block_buffer.std140_instance_array_both):
// rewriteBlockAccess used to assume the wrapper shape's content was always
// a storage buffer's runtime array, unconditionally casting to
// `RuntimeArrayType` and asserting on a uniform block's own field struct
// instead.

// CHECK-LABEL: llvm.func @read_element
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<(array<4 x f32>)>, 2, 0>
// CHECK: %[[FIELD:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[ELEM:.*]] = llvm.getelementptr inbounds %[[FIELD]][0, %{{.*}}]
// CHECK: llvm.load %[[ELEM]] : !llvm.ptr<12> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @cb bind(0, 2) : !spirv.ptr<!spirv.struct<(!spirv.struct<(!spirv.array<4 x f32, stride=4> [0])> [0])>, Uniform>
  spirv.func @read_element(%idx : i32) -> f32 "None" {
    %0 = spirv.mlir.addressof @cb : !spirv.ptr<!spirv.struct<(!spirv.struct<(!spirv.array<4 x f32, stride=4> [0])> [0])>, Uniform>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0, %c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.struct<(!spirv.array<4 x f32, stride=4> [0])> [0])>, Uniform>, i32, i32, i32 -> !spirv.ptr<f32, Uniform>
    %v = spirv.Load "Uniform" %ac : f32
    spirv.ReturnValue %v : f32
  }
}

// -----

// Roadmap F12a: a std140 uniform buffer array's own element stride (16
// bytes for this scalar `uint`) does not equal its element's own natural
// size (4 bytes) the way a std430 storage buffer array's always does --
// unlike every other case in this file, MLIR's own `spirv::ArrayType`
// conversion (`convertArrayType` in SPIRVToLLVM.cpp) refuses to convert an
// array whose stride mismatches its element's natural size at all, so this
// used to fail `spirv.AccessChain` legalization outright
// (`dEQP-VK.pipeline.monolithic.push_descriptor.compute.
// incremental_updates*`). The real stride is instead carried as the
// handle's own third integer parameter (see
// `feme::spirv::convertUniformArrayContent`'s comment), read back by
// `feme::cpu::SPIRVResourceLoweringPass` to multiply the dynamic array
// index by, exactly as a storage buffer's own dynamic index already is.

// CHECK-LABEL: llvm.func @read_std140_element
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.array<0 x i32>, 2, 0, 16>
// CHECK: %[[ELEM:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: llvm.load %[[ELEM]] : !llvm.ptr<12> -> i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ubo bind(0, 5) : !spirv.ptr<!spirv.struct<(!spirv.array<16 x i32, stride=16> [0]), Block>, Uniform>
  spirv.func @read_std140_element(%idx : i32) -> i32 "None" {
    %0 = spirv.mlir.addressof @ubo : !spirv.ptr<!spirv.struct<(!spirv.array<16 x i32, stride=16> [0]), Block>, Uniform>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.array<16 x i32, stride=16> [0]), Block>, Uniform>, i32, i32 -> !spirv.ptr<i32, Uniform>
    %v = spirv.Load "Uniform" %ac : i32
    spirv.ReturnValue %v : i32
  }
}
