// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// (Roadmap L211) Unlike the `Workgroup` struct-member case in
// spirv-to-llvm-matrix-workgroup-whole-store-load.mlir (which genuinely
// needs its matrix member's column vectors *tightened* to match a real,
// declared byte layout shared with a sibling field), a `Function`-storage
// local matrix variable has no declared layout of its own to reconcile at
// all: its `alloca` (built independently, from the same "natural"/ABI-
// rounded `spirv::MatrixType` conversion every matrix *value* already
// uses) is the only thing that ever needs to agree with a whole-matrix
// store here, and it already does, without any tightening.
//
// TightMatrixStorePattern/TightMatrixLoadPattern used to apply their
// tightening *unconditionally*, regardless of storage class -- silently
// disagreeing with this alloca's own untightened declaration whenever a
// column count wasn't a power of two (e.g. `vec3` columns), and, worse,
// disagreeing with any dynamically-indexed element read
// (`spirv.AccessChain` computing `A[row][col]`) that fell through to
// MLIR's own generic, unmodified `AccessChain` pattern -- which, having no
// idea a whole-matrix store here was ever tightened, kept computing its
// own GEP against the plain, untightened alloca type. That mismatch
// corrupted every element whose tight-vs-natural byte offset genuinely
// differed (reduced from `offload-test-suite`'s own
// `Basic/Matrix/matrix_splat_cast.test`: a `float2x3` local splat-
// initialized then read back element-by-element with runtime `row`/`col`
// indices got the very last element wrong, landing 4 bytes past whatever
// the store ever wrote).
//
// This test's alloca is declared, stored, and loaded all using the same,
// plain, natural `!llvm.array<2 x vector<3xf32>>` shape throughout --
// no `feme.tight_vector` wrapping anywhere -- confirming
// TightMatrixStorePattern/TightMatrixLoadPattern now both defer for
// `Function` storage, leaving this path fully consistent with whatever
// alloca declaration and AccessChain reads already assume.

// CHECK-LABEL: llvm.func @store_load_function_matrix
// CHECK: %[[ALLOCA:.*]] = llvm.alloca %{{.*}} x !llvm.array<2 x vector<3xf32>>
// CHECK-NOT: feme.tight_vector
// CHECK: %[[NATURAL:.*]] = llvm.mlir.constant{{.*}} : !llvm.array<2 x vector<3xf32>>
// CHECK: llvm.store %[[NATURAL]], %[[ALLOCA]] : !llvm.array<2 x vector<3xf32>>, !llvm.ptr
// CHECK: llvm.load %[[ALLOCA]] : !llvm.ptr -> !llvm.array<2 x vector<3xf32>>
// CHECK-NOT: feme.tight_vector
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @store_load_function_matrix() -> !spirv.matrix<2 x vector<3xf32>> "None" {
    %0 = spirv.Variable : !spirv.ptr<!spirv.matrix<2 x vector<3xf32>>, Function>
    %m = spirv.Constant dense<[[1.0, 2.0], [3.0, 4.0], [5.0, 6.0]]> : !spirv.matrix<2 x vector<3xf32>>
    spirv.Store "Function" %0, %m : !spirv.matrix<2 x vector<3xf32>>
    %v = spirv.Load "Function" %0 : !spirv.matrix<2 x vector<3xf32>>
    spirv.ReturnValue %v : !spirv.matrix<2 x vector<3xf32>>
  }
}
