// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap K6a (scalarBlockLayout): a *2-level nested* array of matrices
// (`!spirv.array<3 x !spirv.array<4 x !spirv.matrix<2 x vector<2xf32>>>>`,
// `dEQP-VK.ubo.2_level_array.scalar.*`'s own shape) declared immediately
// after a leading scalar with no gap at all (`f32 [0]`, matrix array
// `[4]`) passed ordinary type conversion (a matrix's own column-major
// `!llvm.array<NumColumns x Column>` representation nests through an
// outer `spirv::ArrayType` with no trouble), but failed
// `rewriteBlockAccess`'s own standalone `TypeConverter::convertType`
// call used for `spirv.VulkanBuffer`-handle-based (UBO/SSBO) resource
// access: with the matrix's own column `vector<2xf32>` left as a real
// (not yet tight-vector-marker-substituted) SIMD vector,
// `layOutStructIfOffsetsMatch`'s own ABI-alignment pre-check saw a
// `vec2`'s natural 8-byte alignment rounding `Cursor=4` up to `8`, which
// overshoots this member's own declared `Offset=4` -- rejecting a
// placement a tightly/scalar-packed struct (always built packed, with no
// alignment-driven gap ever implicitly inserted by LLVM) does not
// actually need.
//
// Fixed in two parts: `getTightNestedArrayOfVectorsOrMatricesType`
// (generalizing the existing single-array-level-only substitution to
// peel through any number of nested `spirv::ArrayType` levels before
// bottoming out at the vector/matrix element) is now also reachable from
// `rewriteBlockAccess`'s own standalone conversion call (not just
// `convertOffsetStructTypeIgnoringDecorations`'s member-list-building
// loop and its `WithArraysAndMatrices` retry tier); and
// `layOutStructIfOffsetsMatch`'s own "natural alignment alone overshoots"
// rejection is relaxed -- for its `AllowInteriorPad` last-resort retry
// tier only -- to permit a member's declared `Offset` to sit below what
// its own natural (SIMD-rounded) alignment would otherwise require,
// so long as the raw, unaligned running byte `Cursor` does not itself
// already exceed that `Offset` (the one placement no amount of
// synthesized padding could ever fix).

// CHECK-LABEL: llvm.func @read
// CHECK: %{{.*}} = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (f32, array<3 x array<4 x array<2 x vector<2xf32>>>>)>, 2, 0>
// CHECK: llvm.load %{{.*}} : !llvm.ptr<12> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @cb bind(0, 2) : !spirv.ptr<!spirv.struct<(f32 [0], !spirv.array<3 x !spirv.array<4 x !spirv.matrix<2 x vector<2xf32>>, stride=16>, stride=64> [4, ColMajor, MatrixStride=8]), Block>, Uniform>
  spirv.func @read() -> f32 "None" {
    %0 = spirv.mlir.addressof @cb : !spirv.ptr<!spirv.struct<(f32 [0], !spirv.array<3 x !spirv.array<4 x !spirv.matrix<2 x vector<2xf32>>, stride=16>, stride=64> [4, ColMajor, MatrixStride=8]), Block>, Uniform>
    %c0 = spirv.Constant 0 : si32
    %ac = spirv.AccessChain %0[%c0] : !spirv.ptr<!spirv.struct<(f32 [0], !spirv.array<3 x !spirv.array<4 x !spirv.matrix<2 x vector<2xf32>>, stride=16>, stride=64> [4, ColMajor, MatrixStride=8]), Block>, Uniform>, si32 -> !spirv.ptr<f32, Uniform>
    %v = spirv.Load "Uniform" %ac : f32
    spirv.ReturnValue %v : f32
  }
}
