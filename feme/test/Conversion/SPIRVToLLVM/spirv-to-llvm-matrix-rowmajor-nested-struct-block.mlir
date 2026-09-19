// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap L124(o): a `RowMajor`+`MatrixStride`-decorated matrix reached
// through a *nested struct* member (`b.mB`-shaped, e.g. `cbuffer { S b; }`
// with `struct S { float pad; row_major float2x2 mB; };`) rather than
// directly from the top-level `Block`/`Uniform` struct, found via
// `dEQP-VK.ubo.random.all_per_block_buffers.41`'s own randomized
// struct-shape fuzzer case.
//
// `getMatrixWholeAccess` (used by RowMajorMatrixStorePattern/LoadPattern
// to recognize a whole-matrix `spirv.Store`/`spirv.Load` and reinterpret
// it through the matrix's own physical, RowMajor/MatrixStride-aware
// layout) used to require its non-wrapper branch's `spirv.AccessChain` to
// have *exactly* one member-select index below the top-level `Block`
// struct's own instance-array indices -- true for a matrix that is
// itself a direct top-level member, but false here, where `%elem`'s own
// access chain needs a *second* member-select (into `!inner`) to reach
// the matrix at all. Falling outside that hard-coded shape meant neither
// pattern ever fired, and the plain, wrong (always-logical/natural,
// un-transposed, unpadded) generic `spirv.Store`/`spirv.Load` conversion
// silently took over instead -- corrupting the physical RowMajor layout
// on every real access. Fixed by generalizing that branch into a loop
// that walks through zero-or-more intervening struct-member selects
// (each of which must itself land on a nested `spirv.StructType`) before
// a final member-select that must land on the matrix itself, mirroring
// `peelInstanceArrayPointer`'s own array-nesting-peel pattern.
//
// (This nested member's own natural conversion is also non-representable
// on its own terms -- widening (not tightening) a non-representable
// nested matrix member is a separate half of this same roadmap item; see
// spirv-to-llvm-nested-struct-reorder.mlir's own updated CHECK lines for
// that half. Both halves are required together for a *array* of nested
// structs like this one to stride correctly element-to-element; this
// test's own struct is not itself arrayed, so only exercises the
// whole-matrix-access half directly, but the nested struct's own layout
// below still reflects that other half's fix.)

// CHECK-LABEL: llvm.func @store_load
// CHECK-SAME: (%[[M:.*]]: !llvm.array<2 x vector<2xf32>>) -> !llvm.array<2 x vector<2xf32>>
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (struct<packed (f32, array<12 x i8>, array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>)>, i32)>, 2, 0>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[ELEM:.*]] = llvm.getelementptr inbounds %[[PTR]][0, 2]
// CHECK-SAME: -> !llvm.ptr<12>, !llvm.struct<packed (f32, array<12 x i8>, array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>)>
//
// The value stored is transposed (RowMajor) and padded (each row's own
// natural 8-byte size padded up to the declared 16-byte MatrixStride)
// into the physical shape before the real `llvm.store`, and the inverse
// happens after the real `llvm.load` -- both crossing exactly the same
// flat, packed, per-row-padded type reached by `%[[ELEM]]`; every other
// (arithmetic/return) use of the same value keeps the ordinary logical
// column-major `!llvm.array<2 x vector<2xf32>>` shape.
// CHECK: llvm.store %{{.*}}, %[[ELEM]] : !llvm.array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>, !llvm.ptr<12>
// CHECK: %[[LOADED:.*]] = llvm.load %[[ELEM]] : !llvm.ptr<12> -> !llvm.array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>
// CHECK: llvm.return %{{.*}} : !llvm.array<2 x vector<2xf32>>
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @cb bind(0, 0) : !spirv.ptr<!spirv.struct<(!spirv.struct<(f32 [0], !spirv.matrix<2 x vector<2xf32>> [16, RowMajor, MatrixStride=16])> [0], i32 [48]), Block>, Uniform>
  spirv.func @store_load(%m : !spirv.matrix<2 x vector<2xf32>>) -> !spirv.matrix<2 x vector<2xf32>> "None" {
    %addr = spirv.mlir.addressof @cb : !spirv.ptr<!spirv.struct<(!spirv.struct<(f32 [0], !spirv.matrix<2 x vector<2xf32>> [16, RowMajor, MatrixStride=16])> [0], i32 [48]), Block>, Uniform>
    %c0 = spirv.Constant 0 : si32
    %c1 = spirv.Constant 1 : si32
    %elem = spirv.AccessChain %addr[%c0, %c1] : !spirv.ptr<!spirv.struct<(!spirv.struct<(f32 [0], !spirv.matrix<2 x vector<2xf32>> [16, RowMajor, MatrixStride=16])> [0], i32 [48]), Block>, Uniform>, si32, si32 -> !spirv.ptr<!spirv.matrix<2 x vector<2xf32>>, Uniform>
    spirv.Store "Uniform" %elem, %m : !spirv.matrix<2 x vector<2xf32>>
    %v = spirv.Load "Uniform" %elem : !spirv.matrix<2 x vector<2xf32>>
    spirv.ReturnValue %v : !spirv.matrix<2 x vector<2xf32>>
  }
  spirv.EntryPoint "GLCompute" @store_load
  spirv.ExecutionMode @store_load "LocalSize", 1, 1, 1
}
