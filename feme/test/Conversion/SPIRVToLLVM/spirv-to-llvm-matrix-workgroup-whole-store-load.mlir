// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// (Roadmap L207) A `Workgroup` (shared-memory) struct member whose
// declared layout is representable (the ordinary, natural column-major
// case -- no `RowMajor`/`MatrixStride` padding at all, unlike every
// `spirv-to-llvm-matrix-rowmajor-*`/`spirv-to-llvm-matrix-block-*.mlir`
// case) still needs its `mat4x3` member's own *column* vectors tightened
// (`getTightMatrixType`) whenever that lane count (3) isn't a power of
// two, exactly like an ordinary array-of-vec3 member already gets
// (`convertOffsetStructTypeIgnoringDecorations`'s own per-member loop) --
// see this struct's own explicit `Offset` decorations below, needed for
// `Workgroup` storage's own cross-invocation-deterministic layout
// (`dEQP-VK.memory_model.shared.16bit.*`'s own real shape).
//
// Storing a *whole* matrix value (a `spirv.Constant`, here) directly into
// such a member via `spirv.AccessChain` + `spirv.Store` used to convert
// the stored value through the generic, deliberately "natural"/ABI-
// rounded `spirv::MatrixType` addConversion (16 bytes/column for a
// `vec3` column, rounded up from vec3's own real 12-byte tight size) --
// disagreeing with the member's own tightened (12-byte/column) type, and
// so overflowing 16 bytes into whatever field followed (here, `f16 [48]`,
// declared immediately after the matrix's own tight 48-byte footprint).
// TightMatrixStorePattern (SPIRVToLLVMPatterns.cpp) reconciles the two by
// reassembling the stored value into the member's own tight type before
// storing.

// The struct's own second member (the trailing `f16`) sits exactly 48
// bytes after the matrix's own start -- 4 tight, 12-byte columns, not 4
// ABI-rounded, 16-byte ones (which would place it at byte 64 instead).
// CHECK: llvm.mlir.global external @s() {{.*}} : !llvm.struct<packed (array<4 x struct<"feme.tight_vector{{[.0-9]*}}", (array<3 x f32>)>>, f16)>
// The stored constant (the natural `!llvm.array<4 x vector<3xf32>>` a
// bare matrix constant always converts to) is reassembled column by
// column into the same tight-vector-wrapped shape as the member itself
// (`llvm.insertvalue`s building a `feme.tight_vector` struct per column)
// before the final `llvm.store`, not left/stored as the plain, wider
// natural array.
// CHECK-LABEL: llvm.func @store_whole_matrix
// CHECK: %[[NATURAL:.*]] = llvm.mlir.constant{{.*}} : !llvm.array<4 x vector<3xf32>>
// CHECK: %[[TIGHT:.*]] = llvm.insertvalue %{{.*}}, %{{.*}}[3] : !llvm.array<4 x struct<"feme.tight_vector{{[.0-9]*}}", (array<3 x f32>)>>
// CHECK: llvm.store %[[TIGHT]], %{{.*}} : !llvm.array<4 x struct<"feme.tight_vector{{[.0-9]*}}", (array<3 x f32>)>>, !llvm.ptr<3>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @s : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<3xf32>> [0], f16 [48])>, Workgroup>
  spirv.func @store_whole_matrix() -> () "None" {
    %0 = spirv.mlir.addressof @s : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<3xf32>> [0], f16 [48])>, Workgroup>
    %c0 = spirv.Constant 0 : i32
    %m = spirv.Constant dense<[[1.0, 4.0, 7.0, 10.0], [2.0, 5.0, 8.0, 11.0], [3.0, 6.0, 9.0, 12.0]]> : !spirv.matrix<4 x vector<3xf32>>
    %ac = spirv.AccessChain %0[%c0] : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<3xf32>> [0], f16 [48])>, Workgroup>, i32 -> !spirv.ptr<!spirv.matrix<4 x vector<3xf32>>, Workgroup>
    spirv.Store "Workgroup" %ac, %m : !spirv.matrix<4 x vector<3xf32>>
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @store_whole_matrix
  spirv.ExecutionMode @store_whole_matrix "LocalSize", 1, 1, 1
}

// -----

// (Roadmap L207) The `spirv.Load` counterpart (TightMatrixLoadPattern):
// loading that same whole-matrix member back out must read using the
// member's own real, tight type (matching the struct's own physical
// layout, and whatever TightMatrixStorePattern above wrote there), then
// unwrap the result back to the ordinary, natural `!llvm.array<4 x
// vector<3xf32>>` shape every other matrix consumer already expects --
// not a plain, wrongly-strided `llvm.load` of the natural (untightened)
// type, which would read 16 bytes/column from a location only 12
// bytes/column apart, pulling in whatever bytes follow each column
// instead of the next column's own real data.

// CHECK-LABEL: llvm.func @load_whole_matrix
// CHECK: %[[LOADED:.*]] = llvm.load %{{.*}} : !llvm.ptr<3> -> !llvm.array<4 x struct<"feme.tight_vector{{[.0-9]*}}", (array<3 x f32>)>>
// CHECK: %[[COL0:.*]] = llvm.extractvalue %[[LOADED]][0] : !llvm.array<4 x struct<"feme.tight_vector{{[.0-9]*}}", (array<3 x f32>)>>
// CHECK: llvm.extractvalue %[[COL0]][0] : !llvm.struct<"feme.tight_vector{{[.0-9]*}}", (array<3 x f32>)>
// The final result reassembles back to the ordinary, natural
// `!llvm.array<4 x vector<3xf32>>` shape, not the tight one.
// CHECK: llvm.return %{{.*}} : !llvm.array<4 x vector<3xf32>>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @s : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<3xf32>> [0], f16 [48])>, Workgroup>
  spirv.func @load_whole_matrix() -> !spirv.matrix<4 x vector<3xf32>> "None" {
    %0 = spirv.mlir.addressof @s : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<3xf32>> [0], f16 [48])>, Workgroup>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0] : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<3xf32>> [0], f16 [48])>, Workgroup>, i32 -> !spirv.ptr<!spirv.matrix<4 x vector<3xf32>>, Workgroup>
    %v = spirv.Load "Workgroup" %ac : !spirv.matrix<4 x vector<3xf32>>
    spirv.ReturnValue %v : !spirv.matrix<4 x vector<3xf32>>
  }
}
