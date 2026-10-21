// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap L124(c): `spirv.CopyLogical` (SPIR-V opcode 400, added in SPIR-V
// 1.4) copies a value between two "logically compatible" types --
// recursively the same shape, ignoring per-member layout decorations --
// that need not be identical SPIR-V types at all. `dEQP-VK.compute.pipeline
// .basic.undefined_values`'s own real shader emits exactly this: an
// uninitialized `Function`-storage local struct (no explicit layout) is
// `OpCopyLogical`'d into a `StorageBuffer` block member's own struct type
// (an explicit std430 layout) before being stored.

// When the two types convert to the exact same LLVM type already (the
// common case whenever neither side's own explicit layout needs any
// padding a natural/tight conversion wouldn't already produce), the copy
// erases entirely, exactly like `spirv.CopyObject`.

// CHECK-LABEL: llvm.func @copy_logical_identity
// CHECK: %{{.*}} = llvm.load %{{.*}} : !llvm.ptr -> !llvm.struct<packed (i32, i32, array<2 x i32>)>
// CHECK-NOT: llvm.insertvalue
// CHECK: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.4, [Shader], [SPV_KHR_storage_buffer_storage_class]> {
  spirv.GlobalVariable @outputs bind(0, 0) : !spirv.ptr<!spirv.struct<Outputs, (i32 [0], i32 [4], !spirv.array<2 x i32, stride=4> [8]), Block>, StorageBuffer>
  spirv.func @copy_logical_identity() "None" {
    %outputs = spirv.mlir.addressof @outputs : !spirv.ptr<!spirv.struct<Outputs, (i32 [0], i32 [4], !spirv.array<2 x i32, stride=4> [8]), Block>, StorageBuffer>
    %v = spirv.Variable : !spirv.ptr<!spirv.struct<Local, (i32, i32, !spirv.array<2 x i32>)>, Function>
    %loaded = spirv.Load "Function" %v : !spirv.struct<Local, (i32, i32, !spirv.array<2 x i32>)>
    %copied = spirv.CopyLogical %loaded : !spirv.struct<Local, (i32, i32, !spirv.array<2 x i32>)> to !spirv.struct<Outputs, (i32 [0], i32 [4], !spirv.array<2 x i32, stride=4> [8]), Block>
    %c0 = spirv.Constant 0 : i32
    %dst = spirv.AccessChain %outputs[%c0] : !spirv.ptr<!spirv.struct<Outputs, (i32 [0], i32 [4], !spirv.array<2 x i32, stride=4> [8]), Block>, StorageBuffer>, i32 -> !spirv.ptr<i32, StorageBuffer>
    spirv.Store "StorageBuffer" %dst, %c0 : i32
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @copy_logical_identity
  spirv.ExecutionMode @copy_logical_identity "LocalSize", 1, 1, 1
}

// -----

// When the destination's own explicit layout needs an alignment gap the
// source side's natural/tight conversion has no equivalent of at all (here,
// a std430-style 8-byte gap before the trailing array, forcing a synthetic
// `[8 x i8]` padding member into the destination's own converted LLVM
// struct -- see convertOffsetStructTypeIgnoringDecorations), the copy
// rebuilds the value leaf-by-leaf via `extractvalue`/`insertvalue`,
// remapping each side's own declared member index to its real physical
// field index independently (getStructMemberPhysicalIndex) -- the
// destination's own trailing array member lands at physical index 3 (past
// the synthetic padding member at index 2), not the declared index 2 the
// source side still uses.

// CHECK-LABEL: llvm.func @copy_logical_needs_padding
// CHECK: %[[LOADED:.*]] = llvm.load %{{.*}} : !llvm.ptr -> !llvm.struct<packed (i32, i32, array<2 x i32>)>
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : !llvm.struct<packed (i32, i32, array<8 x i8>, array<2 x i32>)>
// CHECK: %[[M0:.*]] = llvm.extractvalue %[[LOADED]][0]
// CHECK: %[[R0:.*]] = llvm.insertvalue %[[M0]], %[[POISON]][0]
// CHECK: %[[M1:.*]] = llvm.extractvalue %[[LOADED]][1]
// CHECK: %[[R1:.*]] = llvm.insertvalue %[[M1]], %[[R0]][1]
// CHECK: %[[M2:.*]] = llvm.extractvalue %[[LOADED]][2]
// CHECK: llvm.insertvalue %[[M2]], %[[R1]][3]
spirv.module Logical GLSL450 requires #spirv.vce<v1.4, [Shader], [SPV_KHR_storage_buffer_storage_class]> {
  spirv.GlobalVariable @outputs bind(0, 0) : !spirv.ptr<!spirv.struct<Outputs, (i32 [0], i32 [4], !spirv.array<2 x i32, stride=4> [16]), Block>, StorageBuffer>
  spirv.func @copy_logical_needs_padding() "None" {
    %outputs = spirv.mlir.addressof @outputs : !spirv.ptr<!spirv.struct<Outputs, (i32 [0], i32 [4], !spirv.array<2 x i32, stride=4> [16]), Block>, StorageBuffer>
    %v = spirv.Variable : !spirv.ptr<!spirv.struct<Local, (i32, i32, !spirv.array<2 x i32>)>, Function>
    %loaded = spirv.Load "Function" %v : !spirv.struct<Local, (i32, i32, !spirv.array<2 x i32>)>
    %copied = spirv.CopyLogical %loaded : !spirv.struct<Local, (i32, i32, !spirv.array<2 x i32>)> to !spirv.struct<Outputs, (i32 [0], i32 [4], !spirv.array<2 x i32, stride=4> [16]), Block>
    %c0 = spirv.Constant 0 : i32
    %dst = spirv.AccessChain %outputs[%c0] : !spirv.ptr<!spirv.struct<Outputs, (i32 [0], i32 [4], !spirv.array<2 x i32, stride=4> [16]), Block>, StorageBuffer>, i32 -> !spirv.ptr<i32, StorageBuffer>
    spirv.Store "StorageBuffer" %dst, %c0 : i32
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @copy_logical_needs_padding
  spirv.ExecutionMode @copy_logical_needs_padding "LocalSize", 1, 1, 1
}

// -----

// `OpCopyLogical`'s own two SPIR-V types can also differ at a matrix leaf
// nested inside each struct: here, a plain `Function`-storage local's
// natural matrix member (col-major, no explicit layout) is copied into a
// `StorageBuffer` block member whose own `RowMajor`/`MatrixStride`
// decorations make it non-representable by the ordinary (logical)
// conversion -- `getPhysicalMatrixMemberType`'s own row-major,
// `vector`-free substitution. Converting between these two forms needs a
// genuine value-level transpose (reusing RowMajorMatrixStorePattern's own
// gather logic, factored into convertLogicalMatrixToPhysical), not just a
// type-level rebuild: each physical "major entry" (one whole output row)
// gathers one lane from every one of the four logical columns.

// CHECK-LABEL: llvm.func @copy_logical_matrix_row_major
// CHECK-COUNT-16: llvm.extractelement
// CHECK: llvm.store %{{.*}}, %{{.*}} : !llvm.struct<packed (array<4 x array<4 x f32>>)>, !llvm.ptr
spirv.module Logical GLSL450 requires #spirv.vce<v1.4, [Shader], [SPV_KHR_storage_buffer_storage_class]> {
  spirv.GlobalVariable @outputs bind(0, 0) : !spirv.ptr<!spirv.struct<Outputs, (!spirv.matrix<4 x vector<4xf32>> [0, RowMajor, MatrixStride=16]), Block>, StorageBuffer>
  spirv.func @copy_logical_matrix_row_major() "None" {
    %outputs = spirv.mlir.addressof @outputs : !spirv.ptr<!spirv.struct<Outputs, (!spirv.matrix<4 x vector<4xf32>> [0, RowMajor, MatrixStride=16]), Block>, StorageBuffer>
    %v = spirv.Variable : !spirv.ptr<!spirv.struct<Local, (!spirv.matrix<4 x vector<4xf32>>)>, Function>
    %loaded = spirv.Load "Function" %v : !spirv.struct<Local, (!spirv.matrix<4 x vector<4xf32>>)>
    %copied = spirv.CopyLogical %loaded : !spirv.struct<Local, (!spirv.matrix<4 x vector<4xf32>>)> to !spirv.struct<Outputs, (!spirv.matrix<4 x vector<4xf32>> [0, RowMajor, MatrixStride=16]), Block>
    spirv.Store "StorageBuffer" %outputs, %copied : !spirv.struct<Outputs, (!spirv.matrix<4 x vector<4xf32>> [0, RowMajor, MatrixStride=16]), Block>
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @copy_logical_matrix_row_major
  spirv.ExecutionMode @copy_logical_matrix_row_major "LocalSize", 1, 1, 1
}

// -----

// `OpCopyLogical`'s two array types can also disagree on `ArrayStride`
// alone, with no matrix or struct-offset gap involved at all: here a
// `Function`-storage local's naturally-tight `f32` array (`stride=4`,
// matching `sizeof(f32)` exactly, so no substitution at all) is copied
// into a `StorageBuffer` block member whose own declared `stride=16`
// (e.g. a `-fvk-use-dx-layout` array-of-scalars, one register per
// element) forces `convertArrayTypeIgnoringDecorations`'s own opaque,
// `16`-byte stand-in substitution for each element -- a same-bytes
// reinterpret (reinterpretLeafValue), not any real data movement: each
// `f32` lane's own bytes land unchanged at its stand-in's own leading 4
// bytes, memory-roundtripped through a scratch `llvm.alloca`.

// CHECK-LABEL: llvm.func @copy_logical_array_stride
// CHECK: %[[LOADED:.*]] = llvm.load %{{.*}} : !llvm.ptr -> !llvm.struct<packed (array<2 x f32>)>
// CHECK: %[[MEMBER:.*]] = llvm.extractvalue %[[LOADED]][0]
// CHECK: %[[LANE0:.*]] = llvm.extractvalue %[[MEMBER]][0]
// CHECK: %[[SLOT0:.*]] = llvm.alloca %{{.*}} x !llvm.array<16 x i8>
// CHECK: llvm.store %[[LANE0]], %[[SLOT0]] : f32, !llvm.ptr
// CHECK: llvm.load %[[SLOT0]] : !llvm.ptr -> !llvm.array<16 x i8>
// CHECK: %[[LANE1:.*]] = llvm.extractvalue %[[MEMBER]][1]
// CHECK: %[[SLOT1:.*]] = llvm.alloca %{{.*}} x !llvm.array<16 x i8>
// CHECK: llvm.store %[[LANE1]], %[[SLOT1]] : f32, !llvm.ptr
// CHECK: llvm.load %[[SLOT1]] : !llvm.ptr -> !llvm.array<16 x i8>
// CHECK: llvm.store %{{.*}}, %{{.*}} : !llvm.struct<packed (array<2 x array<16 x i8>>)>, !llvm.ptr
spirv.module Logical GLSL450 requires #spirv.vce<v1.4, [Shader], [SPV_KHR_storage_buffer_storage_class]> {
  spirv.GlobalVariable @outputs bind(0, 0) : !spirv.ptr<!spirv.struct<Outputs, (!spirv.array<2 x f32, stride=16> [0]), Block>, StorageBuffer>
  spirv.func @copy_logical_array_stride() "None" {
    %outputs = spirv.mlir.addressof @outputs : !spirv.ptr<!spirv.struct<Outputs, (!spirv.array<2 x f32, stride=16> [0]), Block>, StorageBuffer>
    %v = spirv.Variable : !spirv.ptr<!spirv.struct<Local, (!spirv.array<2 x f32, stride=4>)>, Function>
    %loaded = spirv.Load "Function" %v : !spirv.struct<Local, (!spirv.array<2 x f32, stride=4>)>
    %copied = spirv.CopyLogical %loaded : !spirv.struct<Local, (!spirv.array<2 x f32, stride=4>)> to !spirv.struct<Outputs, (!spirv.array<2 x f32, stride=16> [0]), Block>
    spirv.Store "StorageBuffer" %outputs, %copied : !spirv.struct<Outputs, (!spirv.array<2 x f32, stride=16> [0]), Block>
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @copy_logical_array_stride
  spirv.ExecutionMode @copy_logical_array_stride "LocalSize", 1, 1, 1
}

// -----

// A NESTED array adds one more wrinkle on top of `copy_logical_array_stride`
// above: the outer array's own declared `ArrayStride` can force a
// byte-blob substitution on *one* side's inner array but not the
// other's, when the inner array's own natural size happens to equal one
// side's outer stride but not the other's (mirrors the real
// `nested_arrays_different_inner_stride`/`..._outer_stride` CTS cases
// that exposed this). Here the SRC's inner `array<2xf32, stride=4>`
// has a natural size of 8 bytes, smaller than the outer `stride=16`
// forced on it, so it gets substituted with an opaque `array<16xi8>`
// stand-in (`convertArrayTypeIgnoringDecorations`); the DST's own inner
// array shares the identical logical shape and stride, so ITS outer
// `stride=16` is simultaneously representable another way: the whole
// point here is that recursing into the inner array must reinterpret
// SRC's opaque per-element blob back into the natural
// `array<2xf32>` shape (undoing the OUTER-array-driven substitution)
// before structurally copying it, not simply forward the parent's
// hint straight down (which would read the inner `f32`s' bytes at the
// wrong offsets).

// CHECK-LABEL: llvm.func @copy_logical_nested_array_asym
// CHECK: %[[LOADED:.*]] = llvm.load %{{.*}} : !llvm.ptr -> !llvm.struct<packed (array<2 x array<16 x i8>>)>
// CHECK: %[[MEMBER:.*]] = llvm.extractvalue %[[LOADED]][0]
// CHECK: %[[OUTER0:.*]] = llvm.extractvalue %[[MEMBER]][0]
// CHECK: %[[SLOT0:.*]] = llvm.alloca %{{.*}} x !llvm.array<16 x i8>
// CHECK: llvm.store %[[OUTER0]], %[[SLOT0]] : !llvm.array<16 x i8>, !llvm.ptr
// CHECK: llvm.load %[[SLOT0]] : !llvm.ptr -> !llvm.array<2 x f32>
// CHECK: %[[OUTER1:.*]] = llvm.extractvalue %[[MEMBER]][1]
// CHECK: %[[SLOT1:.*]] = llvm.alloca %{{.*}} x !llvm.array<16 x i8>
// CHECK: llvm.store %[[OUTER1]], %[[SLOT1]] : !llvm.array<16 x i8>, !llvm.ptr
// CHECK: llvm.load %[[SLOT1]] : !llvm.ptr -> !llvm.array<2 x f32>
// CHECK: llvm.store %{{.*}}, %{{.*}} : !llvm.struct<packed (array<2 x array<2 x f32>>)>, !llvm.ptr
spirv.module Logical GLSL450 requires #spirv.vce<v1.4, [Shader], [SPV_KHR_storage_buffer_storage_class]> {
  spirv.GlobalVariable @outputs bind(0, 0) : !spirv.ptr<!spirv.struct<Outputs, (!spirv.array<2 x !spirv.array<2 x f32, stride=4>, stride=8> [0]), Block>, StorageBuffer>
  spirv.func @copy_logical_nested_array_asym() "None" {
    %outputs = spirv.mlir.addressof @outputs : !spirv.ptr<!spirv.struct<Outputs, (!spirv.array<2 x !spirv.array<2 x f32, stride=4>, stride=8> [0]), Block>, StorageBuffer>
    %v = spirv.Variable : !spirv.ptr<!spirv.struct<Local, (!spirv.array<2 x !spirv.array<2 x f32, stride=4>, stride=16>)>, Function>
    %loaded = spirv.Load "Function" %v : !spirv.struct<Local, (!spirv.array<2 x !spirv.array<2 x f32, stride=4>, stride=16>)>
    %copied = spirv.CopyLogical %loaded : !spirv.struct<Local, (!spirv.array<2 x !spirv.array<2 x f32, stride=4>, stride=16>)> to !spirv.struct<Outputs, (!spirv.array<2 x !spirv.array<2 x f32, stride=4>, stride=8> [0]), Block>
    spirv.Store "StorageBuffer" %outputs, %copied : !spirv.struct<Outputs, (!spirv.array<2 x !spirv.array<2 x f32, stride=4>, stride=8> [0]), Block>
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @copy_logical_nested_array_asym
  spirv.ExecutionMode @copy_logical_nested_array_asym "LocalSize", 1, 1, 1
}
