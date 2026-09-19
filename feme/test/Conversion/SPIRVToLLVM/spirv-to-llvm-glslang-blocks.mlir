// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks the shapes glslang itself emits for a storage/uniform buffer
// block, as opposed to FeMe's own upstream HLSL resource representation
// (see spirv-to-llvm-storage-buffer.mlir/spirv-to-llvm-uniform-buffer.mlir):
// glslang never wraps a block in an extra single-member struct, so its own
// `Block`/`BufferBlock`-decorated struct is free to declare more than one
// member directly.

// A pre-SPIR-V-1.3 SSBO: `Uniform` storage class, `BufferBlock` decoration,
// rather than `StorageBuffer`/`Block`. Otherwise identical to the ordinary
// single-member storage-buffer wrapper case. The handle's own storage-class
// integer parameter is `12` (`StorageBuffer`), not `2` (the pointer's own
// literal `Uniform` storage class) -- see convertBufferBlockType's own
// comment (roadmap L124(l)) for why the literal value must not be forwarded
// here: this legacy spelling is indistinguishable from a genuine uniform
// block by storage class and writability alone whenever it also happens to
// be non-writable (a `readonly buffer`, as here), so the marker itself must
// unambiguously record "this is a storage buffer" instead.
//
// CHECK-LABEL: llvm.func @legacy_ssbo
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.array<0 x f32>, 12, 1, 4>
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
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (i32, array<0 x f32>)>, 12, 1>
// CHECK: %[[FIELD:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: llvm.load %[[FIELD]] : !llvm.ptr<11> -> i32

// CHECK-LABEL: llvm.func @array_element
// CHECK: %[[HANDLE2:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (i32, array<0 x f32>)>, 12, 1>
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

// (Roadmap L124(l)) A `Block`-decorated storage buffer struct whose
// trailing runtime array's own element is itself a struct whose natural
// (packed) size undershoots the array's declared `ArrayStride` -- the
// shape `dEQP-VK.ssbo.unsized_nested_struct_array.*` hits, e.g.
// `struct T { float a; }; buffer Block { int header; T t[]; };`, where
// `T`'s own real size (4 bytes) is smaller than the 16-byte stride every
// std140/std430 array rounds its element up to regardless of the
// element's own size. There is no wrapper here (`header` is a second
// member alongside `t`), so this array never reaches
// `convertBufferBlockType`'s own explicit third-integer-parameter stride
// mechanism (see `header_field`/`array_element` above) -- the only place
// this stride can be recorded is `T`'s own converted LLVM type, which
// must therefore carry the padding itself (mirroring how a *fixed*-size
// array's identified-struct element already does, see
// `convertArrayTypeIgnoringDecorations`), or every array index past the
// first computes the wrong byte offset via `getelementptr`.

// CHECK-LABEL: llvm.func @nested_struct_array_element
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (i32, array<12 x i8>, array<0 x struct<packed (f32, array<12 x i8>)>>)>, 12, 1>
// CHECK: %[[MEMBER:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[ELEM:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, %{{.*}}, 0]
// CHECK: llvm.load %[[ELEM]] : !llvm.ptr<11> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @nb bind(0, 6) : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.rtarray<!spirv.struct<(f32 [0])>, stride=16> [16]), Block>, StorageBuffer>
  spirv.func @nested_struct_array_element(%idx : i32) -> f32 "None" {
    %0 = spirv.mlir.addressof @nb : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.rtarray<!spirv.struct<(f32 [0])>, stride=16> [16]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %ac = spirv.AccessChain %0[%c1, %idx, %c0] : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.rtarray<!spirv.struct<(f32 [0])>, stride=16> [16]), Block>, StorageBuffer>, i32, i32, i32 -> !spirv.ptr<f32, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : f32
    spirv.ReturnValue %v : f32
  }
}

// -----

// (Roadmap L124(n)) A `Block`-decorated storage buffer struct whose
// trailing runtime array's own element is a *vector* (rather than a
// struct, see `nested_struct_array_element` above) whose natural size
// undershoots the array's declared `ArrayStride` -- the shape
// `dEQP-VK.ssbo.layout.random.*` hits, e.g.
// `buffer Block { int header; ivec2 d[]; };`, where `ivec2`'s own real
// size (8 bytes) is smaller than the 16-byte stride every std140/std430
// array rounds its element up to regardless of the element's own size.
// `padStructToSize` is a deliberate no-op for this non-struct element, so
// this shape needs the same byte-array-stand-in substitution
// `convertArrayTypeIgnoringDecorations` already applies for a *fixed*-size
// array's own scalar/vector element, applied here instead.

// CHECK-LABEL: llvm.func @vector_array_element
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (i32, array<12 x i8>, array<0 x array<16 x i8>>)>, 12, 1>
// CHECK: %[[MEMBER:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[ELEM:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, %{{.*}}, %{{.*}}]
// CHECK: llvm.load %[[ELEM]] : !llvm.ptr<11> -> i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @vb bind(0, 7) : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.rtarray<vector<2xi32>, stride=16> [16]), Block>, StorageBuffer>
  spirv.func @vector_array_element(%idx : i32) -> i32 "None" {
    %0 = spirv.mlir.addressof @vb : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.rtarray<vector<2xi32>, stride=16> [16]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %ac = spirv.AccessChain %0[%c1, %idx, %c0] : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.rtarray<vector<2xi32>, stride=16> [16]), Block>, StorageBuffer>, i32, i32, i32 -> !spirv.ptr<i32, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : i32
    spirv.ReturnValue %v : i32
  }
}

// -----

// A `Block`-decorated *uniform* struct with more than one member declared
// directly -- the shape a plain GLSL `uniform UBO { vec4 a; float b; };`
// compiles to, with no wrapper struct either.

// CHECK-LABEL: llvm.func @read_b
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (vector<4xf32>, f32)>, 2, 0>
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
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (array<4 x f32>)>, 2, 0>
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
