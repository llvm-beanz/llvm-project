// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap L124(p): a column-select access into a non-representable
// (non-square, `ColMajor`+`MatrixStride`) *array of matrices* reached
// through a member index >= 1 of a struct that is itself the sole member
// of the outer `Block`/`Uniform` wrapper struct (`getUniformBlockElement`'s
// "sole member is itself a struct" shape -- indistinguishable, by type
// shape alone, from dxc's own `cbuffer`/`ConstantBuffer<T>` convention,
// but here reached the way glslang lowers a plain
// `uniform Block { S s; };`), found via
// `dEQP-VK.ubo.single_struct.per_block_buffer.std140_both`.
//
// `rewriteBlockAccess` always looked up a member's own `RowMajor`/
// `MatrixStride` decorations on `BlockStruct` -- the *outer* struct the
// original base pointer points to -- at `MatrixDecorationMemberIndex`,
// an index that (for this wrapper shape) is actually computed relative
// to `Element.Content`, the *inner* Field struct `S`, not `BlockStruct`
// itself. `BlockStruct` here has only one member (its own sole member,
// `S` as a whole), so any index >= 1 (any member of `S` past its own
// first) crashed `StructType::getMemberDecorations`'s
// `getNumElements() > index` assertion outright -- it never got the
// chance to return a wrong answer, just aborted.
//
// Fixed by introducing `DecorationStruct` (defaulting to `BlockStruct`,
// reassigned to `Element.Content` itself, cast to `StructType`, whenever
// `MatrixDecorationMemberIndex` is set from indexing into a struct-typed
// `Element.Content`) and looking decorations up on `DecorationStruct`
// instead of `BlockStruct` at every call site that needs them.

// CHECK-LABEL: llvm.func @read
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (i32, array<12 x i8>, array<2 x array<3 x struct<packed (array<2 x f32>, array<8 x i8>)>>>)>, 2, 0>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[ROW:.*]] = llvm.getelementptr inbounds %[[PTR]][0, %{{.*}}]
// CHECK-SAME: -> !llvm.ptr<12>, !llvm.array<2 x array<3 x struct<packed (array<2 x f32>, array<8 x i8>)>>>
// CHECK: %[[COL:.*]] = llvm.getelementptr inbounds %[[ROW]][%{{.*}}]
// CHECK-SAME: -> !llvm.ptr<12>, !llvm.array<16 x i8>
// CHECK: llvm.load %[[COL]] : !llvm.ptr<12> -> vector<2xf32>
!S = !spirv.struct<(si32 [0], !spirv.array<2 x !spirv.matrix<3 x vector<2xf32>>, stride=32> [16, ColMajor, MatrixStride=16])>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @cb bind(0, 0) : !spirv.ptr<!spirv.struct<(!S), Block>, Uniform>
  spirv.func @read(%idx : si32, %col : si32) -> vector<2xf32> "None" {
    %addr = spirv.mlir.addressof @cb : !spirv.ptr<!spirv.struct<(!S), Block>, Uniform>
    %c0 = spirv.Constant 0 : si32
    %c1 = spirv.Constant 1 : si32
    %ac = spirv.AccessChain %addr[%c0, %c1, %idx, %col] : !spirv.ptr<!spirv.struct<(!S), Block>, Uniform>, si32, si32, si32, si32 -> !spirv.ptr<vector<2xf32>, Uniform>
    %v = spirv.Load "Uniform" %ac : vector<2xf32>
    spirv.ReturnValue %v : vector<2xf32>
  }
  spirv.EntryPoint "GLCompute" @read
  spirv.ExecutionMode @read "LocalSize", 1, 1, 1
}
