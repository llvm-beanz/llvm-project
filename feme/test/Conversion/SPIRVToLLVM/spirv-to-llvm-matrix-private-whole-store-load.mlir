// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// (Roadmap L242) A bare `Private`-storage `spirv.GlobalVariable` whose
// own declared type is *directly* a matrix -- no wrapping struct at all,
// unlike `spirv-to-llvm-matrix-workgroup-whole-store-load.mlir`'s
// offset-decorated struct member -- has exactly the same "no declared
// tight layout to reconcile with" shape
// `spirv-to-llvm-matrix-function-whole-store-load.mlir`'s `Function`-
// storage local already covers (roadmap L211), just triggered by a
// global instead of an `alloca`: the global's own conversion (whatever
// generic/default `GlobalVariableOp` pattern a bare, non-struct-wrapped
// `Private` global falls through to) always declares it using the
// plain, natural conversion, never `getTightMatrixType`'s tightened one.
//
// TightMatrixStorePattern/TightMatrixLoadPattern used to key this
// exemption purely on `StorageClass::Function`, so a `Private`-storage
// bare matrix global like this one still got tightened -- disagreeing
// with the global's own untightened declared type, and with every
// dynamically-indexed `spirv.AccessChain` element read/write computing
// its own GEP against that same untightened type (reduced from
// `dEQP-VK.pipeline.monolithic.spec_constant.*.composite.matrix.
// {mat2x3,mat3,mat4x3}`'s own real shape: a `mat2x3` spec-constant-
// initialized global summed element-by-element via a runtime `row`/`col`
// loop read every element as a canonical NaN, since the tight store's own
// 24 written bytes fell short of -- and were misaligned within -- the
// natural 32-byte layout the loop's own GEP arithmetic assumed).
//
// This test's global is declared, stored, and loaded all using the same,
// plain, natural `!llvm.array<2 x vector<3xf32>>` shape throughout -- no
// `feme.tight_vector` wrapping anywhere -- confirming
// TightMatrixStorePattern/TightMatrixLoadPattern now also defer for a
// bare (non-`AccessChain`-reached) pointee regardless of storage class,
// not just `Function` storage specifically.

// CHECK: llvm.mlir.global private @s() {{.*}} : !llvm.array<2 x vector<3xf32>>
// CHECK-LABEL: llvm.func @store_load_private_matrix
// CHECK-NOT: feme.tight_vector
// CHECK: %[[NATURAL:.*]] = llvm.mlir.constant{{.*}} : !llvm.array<2 x vector<3xf32>>
// CHECK: llvm.store %[[NATURAL]], %{{.*}} : !llvm.array<2 x vector<3xf32>>, !llvm.ptr
// CHECK: llvm.load %{{.*}} : !llvm.ptr -> !llvm.array<2 x vector<3xf32>>
// CHECK-NOT: feme.tight_vector
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @s : !spirv.ptr<!spirv.matrix<2 x vector<3xf32>>, Private>
  spirv.func @store_load_private_matrix() -> !spirv.matrix<2 x vector<3xf32>> "None" {
    %0 = spirv.mlir.addressof @s : !spirv.ptr<!spirv.matrix<2 x vector<3xf32>>, Private>
    %m = spirv.Constant dense<[[1.0, 2.0], [3.0, 4.0], [5.0, 6.0]]> : !spirv.matrix<2 x vector<3xf32>>
    spirv.Store "Private" %0, %m : !spirv.matrix<2 x vector<3xf32>>
    %v = spirv.Load "Private" %0 : !spirv.matrix<2 x vector<3xf32>>
    spirv.ReturnValue %v : !spirv.matrix<2 x vector<3xf32>>
  }
}

// -----

// The `spirv.AccessChain`-based per-element read counterpart: a bare
// `Private` matrix global's own individual-element `AccessChain` (2
// indices: column, row) must compute its GEP against the *same* natural,
// untightened global type the whole-matrix store above uses -- not the
// tightened `feme.tight_vector`-wrapped type an `AccessChain` reaching a
// genuinely tightened *struct member* (see
// spirv-to-llvm-matrix-workgroup-whole-store-load.mlir) would use.

// CHECK: llvm.mlir.global private @s() {{.*}} : !llvm.array<2 x vector<3xf32>>
// CHECK-LABEL: llvm.func @store_then_element_load
// CHECK-NOT: feme.tight_vector
// CHECK: llvm.store %{{.*}} : !llvm.array<2 x vector<3xf32>>, !llvm.ptr
// CHECK: llvm.getelementptr {{.*}} : (!llvm.ptr, i32, i32, i32) -> !llvm.ptr, !llvm.array<2 x vector<3xf32>>
// CHECK: llvm.load %{{.*}} : !llvm.ptr -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @s : !spirv.ptr<!spirv.matrix<2 x vector<3xf32>>, Private>
  spirv.func @store_then_element_load(%col: i32, %row: i32) -> f32 "None" {
    %0 = spirv.mlir.addressof @s : !spirv.ptr<!spirv.matrix<2 x vector<3xf32>>, Private>
    %m = spirv.Constant dense<[[1.0, 2.0], [3.0, 4.0], [5.0, 6.0]]> : !spirv.matrix<2 x vector<3xf32>>
    spirv.Store "Private" %0, %m : !spirv.matrix<2 x vector<3xf32>>
    %ac = spirv.AccessChain %0[%col, %row] : !spirv.ptr<!spirv.matrix<2 x vector<3xf32>>, Private>, i32, i32 -> !spirv.ptr<f32, Private>
    %e = spirv.Load "Private" %ac : f32
    spirv.ReturnValue %e : f32
  }
}
