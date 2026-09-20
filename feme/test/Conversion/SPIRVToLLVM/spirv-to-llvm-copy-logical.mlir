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
