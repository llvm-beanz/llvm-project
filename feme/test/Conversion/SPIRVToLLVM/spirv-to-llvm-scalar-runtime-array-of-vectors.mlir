// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap K6l: a trailing *runtime-sized array* of a bare, 3-component
// vector member -- e.g. `layout(scalar, ...) buffer Block { uint a; mat2x3
// b; bvec4 c; mediump vec3 d[]; };`'s own `d[]` -- whose declared
// `ArrayStride` (12, tightly packed under
// `-fvk-use-scalar-layout`/`GL_EXT_scalar_block_layout`) is *narrower*
// than the raw `vector<3xf32>` element's own DataLayout-reported size.
// `getDefaultTypeSizeInBits` (upstream MLIR) always rounds a vector's
// innermost-dimension lane count up to the next power of two purely for
// hardware SIMD-register purposes, so a bare `vector<3xf32>` always
// reports 16 bytes regardless of its real 12-byte natural size -- true of
// ordinary std430 (where the declared Stride is also 16, matching) but
// not of this scalar-layout shape, whose declared Stride stays 12.
//
// Found via `dEQP-VK.ssbo.layout.random.scalar.3`
// (`feme::SPIRVToLLVMPatterns.cpp`'s own `RuntimeArrayType` conversion):
// that conversion's own Stride-mismatch retry only ever widened a
// too-small element up to a wider declared Stride (the opposite,
// ordinary direction, e.g. a 4-byte scalar under a 16-byte Stride) --
// never the reverse, narrower-than-element-size direction this shape
// needs, so a bare `d[]` read (any index past the first, since index 0's
// own byte offset is stride-independent) silently computed the wrong
// address, reading bytes belonging to a *later* array element.
//
// Fixed by falling back to `getStridedTightVectorArrayType` (the same
// alignment-free, array-of-scalars marker substitution used elsewhere in
// this file for a fixed-size array of vectors under an identical
// mismatch) whenever a *runtime*-sized array's own vector element's
// natural size exceeds its declared Stride.

// The resource's own declared type substitutes the tight, alignment-free
// marker (an `array<3 x f32>`-wrapping identified struct, not a bare
// `vector<3xf32>`) as `d`'s own array element, so each element's real
// footprint is 12 bytes, matching the declared Stride:
// CHECK-LABEL: llvm.func @read_d
// CHECK: llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (i32, array<2 x array<3 x f32>>, struct<"feme.tight_vector.i32x4", (array<4 x i32>)>, array<0 x struct<"feme.tight_vector.f32x3", (array<3 x f32>)>>)>

// `getpointer`'s own index selects `d` (member 3); the residual GEP
// navigates a dynamic index through `d`'s own tight-marker-element array,
// whose stride is exactly 12 bytes (`sizeof(array<3 x f32>)`), not 16
// (`sizeof(vector<3xf32>)`):
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"
// CHECK: %[[GEP:.*]] = llvm.getelementptr inbounds %[[PTR]][0, %{{.*}}] : {{.*}}, !llvm.array<0 x struct<"feme.tight_vector.f32x3", (array<3 x f32>)>>
// CHECK: llvm.load %[[GEP]]

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ssbo bind(0, 1) : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.matrix<2 x vector<3xf32>> [4, ColMajor, MatrixStride=12], vector<4xi32> [28], !spirv.rtarray<vector<3xf32>, stride=12> [44]), BufferBlock>, Uniform>
  spirv.func @read_d(%idx : i32) -> vector<3xf32> "None" {
    %0 = spirv.mlir.addressof @ssbo : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.matrix<2 x vector<3xf32>> [4, ColMajor, MatrixStride=12], vector<4xi32> [28], !spirv.rtarray<vector<3xf32>, stride=12> [44]), BufferBlock>, Uniform>
    %c3 = spirv.Constant 3 : i32
    %ac = spirv.AccessChain %0[%c3, %idx] : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.matrix<2 x vector<3xf32>> [4, ColMajor, MatrixStride=12], vector<4xi32> [28], !spirv.rtarray<vector<3xf32>, stride=12> [44]), BufferBlock>, Uniform>, i32, i32 -> !spirv.ptr<vector<3xf32>, Uniform>
    %v = spirv.Load "Uniform" %ac : vector<3xf32>
    spirv.ReturnValue %v : vector<3xf32>
  }
}
