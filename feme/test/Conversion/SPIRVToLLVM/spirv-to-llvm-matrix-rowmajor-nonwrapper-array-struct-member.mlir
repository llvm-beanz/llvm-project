// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap L124(s): a `RowMajor`+non-natural-`MatrixStride` matrix reached
// through a *non-wrapper* storage buffer block member that is itself a
// direct, fixed-size array of structs, each containing a matrix member
// -- e.g. `struct Blk { int a; S b[2]; }` where `S` has a matrix member
// -- rather than the array wrapping the matrix directly (already handled,
// roadmap L124(o)/(q)) or the block's sole member being such an array
// reached through the `HasWrapper` shape (already handled, roadmap
// L124(r)). Found via `dEQP-VK.ssbo.layout.random.all_shared_buffer.44`
// and `.nested_structs_arrays.14`, whose own decompiled SPIR-V
// (`--deqp-log-decompiled-spirv=enable`) showed exactly this shape.
//
// `walkStructMembersToMatrix`'s array-nesting-peel loop (shared by both
// `getMatrixWholeAccess`'s non-wrapper and `HasWrapper` branches) only
// recognized a peeled array's inner type being a bare `MatrixType`; it
// had no case for the inner type being a `StructType` instead, so this
// shape was rejected outright, falling back to the generic conversion.
// The struct's own physical layout (including the nested matrix's own
// widening) was already correctly computed by the pre-existing
// per-member struct-conversion logic, but the whole-matrix
// `spirv.Store`/`spirv.Load` reinterpretation (transpose + per-row
// padding) never fired, silently storing/loading the plain logical
// (unpadded, column-major) value straight into the wider physical
// layout -- a memory-size mismatch, not merely a wrong value.
//
// Fixed by extending the array-nesting-peel loop's terminal case: when
// the fully-peeled inner type is a `StructType` (not a bare
// `MatrixType`), consume the array-index selectors already walked and
// continue the same struct-member search from that inner struct, rather
// than declining.

// CHECK-LABEL: llvm.func @store_load
// CHECK-SAME: (%[[M:.*]]: !llvm.array<2 x vector<2xf32>>, %[[IDX:.*]]: i32) -> !llvm.array<2 x vector<2xf32>>
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (i32, array<12 x i8>, array<2 x struct<packed (array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>, vector<4xf32>)>>)>, 12, 1>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]]
// CHECK: %[[ELEM:.*]] = llvm.getelementptr inbounds %[[PTR]][0, %[[IDX]], 0]
// CHECK-SAME: -> !llvm.ptr<12>, !llvm.array<2 x struct<packed (array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>, vector<4xf32>)>>
//
// The value stored is transposed (RowMajor) and padded (each row's own
// natural 8-byte size padded up to the declared 16-byte MatrixStride)
// into the physical shape before the real `llvm.store`, and the inverse
// happens after the real `llvm.load` -- both crossing exactly the same
// flat, packed, per-row-padded type reached by `%[[ELEM]]`.
// CHECK: llvm.store %{{.*}}, %[[ELEM]] : !llvm.array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>, !llvm.ptr<12>
// CHECK: %[[LOADED:.*]] = llvm.load %[[ELEM]] : !llvm.ptr<12> -> !llvm.array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>
// CHECK: llvm.return %{{.*}} : !llvm.array<2 x vector<2xf32>>
!S = !spirv.struct<(!spirv.matrix<2 x vector<2xf32>> [0, RowMajor, MatrixStride=16], vector<4xf32> [32])>
!Blk = !spirv.struct<(i32 [0], !spirv.array<2 x !S, stride=48> [16]), BufferBlock>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ssbo bind(0, 0) : !spirv.ptr<!Blk, Uniform>
  spirv.func @store_load(%m : !spirv.matrix<2 x vector<2xf32>>, %idx : si32) -> !spirv.matrix<2 x vector<2xf32>> "None" {
    %addr = spirv.mlir.addressof @ssbo : !spirv.ptr<!Blk, Uniform>
    %c0 = spirv.Constant 0 : si32
    %c1 = spirv.Constant 1 : si32
    %melem = spirv.AccessChain %addr[%c1, %idx, %c0] : !spirv.ptr<!Blk, Uniform>, si32, si32, si32 -> !spirv.ptr<!spirv.matrix<2 x vector<2xf32>>, Uniform>
    spirv.Store "Uniform" %melem, %m : !spirv.matrix<2 x vector<2xf32>>
    %v = spirv.Load "Uniform" %melem : !spirv.matrix<2 x vector<2xf32>>
    spirv.ReturnValue %v : !spirv.matrix<2 x vector<2xf32>>
  }
  spirv.EntryPoint "GLCompute" @store_load
  spirv.ExecutionMode @store_load "LocalSize", 1, 1, 1
}
