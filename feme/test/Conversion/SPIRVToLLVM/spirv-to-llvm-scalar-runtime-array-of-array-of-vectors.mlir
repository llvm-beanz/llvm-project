// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap K6m: a trailing *runtime-sized array* whose own element is
// *itself* a fixed-size `spirv.array` of a bare, 3-component vector --
// e.g. `layout(scalar, ...) buffer Block { uint a; mediump ivec3 d[][2];
// };`'s own `d[][2]` -- where the *inner* fixed array's declared
// `ArrayStride` (12, tightly packed under
// `-fvk-use-scalar-layout`/`GL_EXT_scalar_block_layout`) is narrower than
// the raw `vector<3xsi32>` element's own DataLayout-reported size (always
// rounded up to the next power-of-two lane count, 16 bytes, for a bare
// 3-component vector -- see `spirv-to-llvm-scalar-runtime-array-of-
// vectors.mlir`'s own comment for why).
//
// Found via `dEQP-VK.ssbo.layout.random.scalar.41` (also covers the 4
// `dEQP-VK.ssbo.layout.3_level_unsized_array.scalar.*` cases, the same
// shape one level more deeply nested): unlike a *direct* `rtarray<vector>`
// member (K6l, fixed above), `convertArrayTypeIgnoringDecorations` (the
// ordinarily-registered `spirv::ArrayType` conversion) itself gives up
// outright (returns null) on the *inner* fixed array here, relying on a
// more specialized caller to retry with a tight substitute -- which a
// direct struct member has (`getTightNestedArrayOfVectorsOrMatricesType`,
// inside `convertOffsetStructTypeIgnoringDecorations`'s own member loop),
// but `spirv::RuntimeArrayType`'s own element-type conversion did not.
// The raw conversion failure propagated straight up through the whole
// runtime array's own conversion returning null, which several steps
// later surfaced as `"failed to legalize operation
// 'spirv.AccessChain'...explicitly marked illegal"` -- a *legalization*
// error, not a miscompile, since no LLVM type for `d`'s own element ever
// existed for the `AccessChain` conversion's own GEP to use at all.
//
// Fixed by retrying via `getTightNestedArrayOfVectorsType` (a
// `Struct`/`Index`-decoration-free sibling of
// `getTightNestedArrayOfVectorsOrMatricesType`, since no matrix can ever
// appear nested this way without its own owning struct member to carry a
// `MatrixStride` decoration) whenever `RuntimeArrayType`'s own element
// conversion fails outright.

// The resource's own declared type substitutes the tight, alignment-free
// marker (an `array<3 x i32>`-wrapping identified struct, not a bare
// `vector<3xsi32>`) as the innermost array's own element, nested inside an
// ordinary fixed `array<2 x ...>` wrapped in turn by the runtime array:
// CHECK-LABEL: llvm.func @read_d
// CHECK: llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (i32, array<12 x i8>, array<0 x array<2 x struct<"feme.tight_vector.i32x3", (array<3 x i32>)>>>)>

// `getpointer`'s own index selects `d` (member 1); the residual GEP
// navigates two dynamic indices -- first through the runtime array, then
// through its own inner fixed `array<2 x ...>` -- down to the tight-
// marker-element array, whose stride is exactly 12 bytes
// (`sizeof(array<3 x i32>)`), not 16 (`sizeof(vector<3xsi32>)`):
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"
// CHECK: %[[GEP:.*]] = llvm.getelementptr inbounds %[[PTR]][0, %{{.*}}, %{{.*}}] : {{.*}}, !llvm.array<0 x array<2 x struct<"feme.tight_vector.i32x3", (array<3 x i32>)>>>
// CHECK: llvm.load %[[GEP]]

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ssbo bind(0, 1) : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.rtarray<!spirv.array<2 x vector<3xsi32>, stride=12>, stride=24> [16]), BufferBlock>, Uniform>
  spirv.func @read_d(%idx1 : i32, %idx2 : i32) -> vector<3xsi32> "None" {
    %0 = spirv.mlir.addressof @ssbo : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.rtarray<!spirv.array<2 x vector<3xsi32>, stride=12>, stride=24> [16]), BufferBlock>, Uniform>
    %c1 = spirv.Constant 1 : i32
    %ac = spirv.AccessChain %0[%c1, %idx1, %idx2] : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.rtarray<!spirv.array<2 x vector<3xsi32>, stride=12>, stride=24> [16]), BufferBlock>, Uniform>, i32, i32, i32 -> !spirv.ptr<vector<3xsi32>, Uniform>
    %v = spirv.Load "Uniform" %ac : vector<3xsi32>
    spirv.ReturnValue %v : vector<3xsi32>
  }
}
