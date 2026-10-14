// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap K6j regression test: matches `dEQP-VK.ubo.single_nested_struct.
// per_block_buffer.scalar_both`'s own `Inner1`/`outer` shape -- a 2-
// member `Block` struct (`!outer`) whose first member is a nested,
// explicitly-offset struct (`!inner1`) containing a scalar-tight
// (`stride=36`, narrower than its matrix columns' own natural
// vector-ABI-rounded size) array-of-`mat3x3` member, and whose second
// member is a plain `vector<2xf32>` declared at the offset exactly
// matching `!inner1`'s own tight natural size (172 bytes) -- no interior
// gap needed at all.
//
// Root cause: `!outer`'s own conversion (`Type.hasOffset() &&
// NestedStructTy.hasOffset()` both true here) reaches
// `convertOffsetStructTypeIgnoringDecorations`'s `VectorOnly` retry tier
// once its first (natural) attempt fails to reproduce `vector<2xf32>`'s
// declared offset, which re-converts `!inner1` via
// `getTightNestedStructType` instead of the ordinary recursive
// conversion. That function's own array-member branch had no
// `substituteArrayOfMatrixElementType`/`getTightNestedArrayOfVectorsOrMatricesType`
// fallback at all -- unlike `convertOffsetStructTypeIgnoringDecorations`'s
// own member loop, which unconditionally re-validates every member's
// matrix representability and substitutes a corrected physical layout
// regardless of whether the member's initial conversion failed outright
// or silently substituted the wrong (natural, vector-ABI-padded, too-
// wide) layout -- so `!inner1`'s own `stride=36` array-of-`mat3x3` member
// either failed to convert at all, or (as was eventually found) silently
// converted to a too-wide `array<4 x array<3 x vector<3xf32>>>` instead
// of the correct, tight `array<4 x array<3 x array<3 x f32>>>`,
// cascading to a whole-struct conversion failure (or a wrong one) that
// surfaced downstream as a `spirv.AccessChain` legalization failure when
// building `!outer`'s own resource-handle type.
//
// Fixed by applying the same `substituteArrayOfMatrixElementType`
// correction (and the existing `getTightNestedArrayOfVectorsOrMatricesType`
// outright-failure fallback) inside `getTightNestedStructType`'s own
// array-member branch that `convertOffsetStructTypeIgnoringDecorations`'s
// member loop already applies unconditionally.

!inner1 = !spirv.struct<(vector<3xsi32> [0], !spirv.array<4 x !spirv.matrix<3 x vector<3xf32>>, stride=36> [12, ColMajor, MatrixStride=12], vector<4xf32> [156])>
!outer = !spirv.struct<(!inner1 [0], vector<2xf32> [172]), Block>

// CHECK-LABEL: llvm.func @read
// CHECK: llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: !llvm.target<"spirv.VulkanBuffer"
// CHECK: llvm.call_intrinsic "llvm.spv.resource.getpointer"
// CHECK: llvm.load %{{.*}} : !llvm.ptr<12> -> vector<2xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @cb bind(0, 2) : !spirv.ptr<!outer, Uniform>
  spirv.func @read() -> vector<2xf32> "None" {
    %addr = spirv.mlir.addressof @cb : !spirv.ptr<!outer, Uniform>
    %c0 = spirv.Constant 0 : si32
    %c1 = spirv.Constant 1 : si32
    %ac = spirv.AccessChain %addr[%c1] : !spirv.ptr<!outer, Uniform>, si32 -> !spirv.ptr<vector<2xf32>, Uniform>
    %v = spirv.Load "Uniform" %ac : vector<2xf32>
    spirv.ReturnValue %v : vector<2xf32>
  }
  spirv.EntryPoint "GLCompute" @read
  spirv.ExecutionMode @read "LocalSize", 1, 1, 1
}
