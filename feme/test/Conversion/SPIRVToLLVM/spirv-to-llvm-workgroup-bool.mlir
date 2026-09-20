// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap L124(v): a `bool` (`i1`, SPIR-V's own `OpTypeBool` representation)
// scalar `Workgroup` global, or a `bool`/`bvec*` member of a `Workgroup`-
// storage struct, lowers cleanly -- reproducing
// dEQP-VK.compute.pipeline.zero_initialize_workgroup_memory.{types.bool,
// composites.2}'s own shapes (a bare scalar `bool` global, and a struct
// mixing a `bool` and a `bvec2` member in with a real scalar). A struct
// member's own `getelementptr` index is always a fixed byte offset baked
// into the struct's layout (never a runtime multiply-by-element-size the
// way array/vector indexing is), and LLVM's data layout already reserves a
// full byte for an `i1`'s own storage, so no special handling is needed
// here at all -- confirmed via direct `mlir-translate -mlir-to-llvmir` +
// `opt -passes=verify` round-tripping of this exact IR. The one shape that
// genuinely cannot use `getelementptr` -- indexing a single lane out of a
// `bool` vector -- is handled separately by
// `BoolVectorLaneAccessChainPattern`/`BoolVectorLaneLoadPattern` (full-
// vector load plus `extractelement`, see the CHECK lines below).

// CHECK-LABEL: llvm.mlir.global external @scalar_bool
// CHECK-SAME: {addr_space = 3 : i32} : i1
// CHECK-LABEL: llvm.mlir.global external @wg
// CHECK-SAME: {addr_space = 3 : i32} : !llvm.struct<packed (i32, i1, vector<2xi1>
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader], []> {
  spirv.GlobalVariable @scalar_bool : !spirv.ptr<i1, Workgroup>
  spirv.GlobalVariable @wg : !spirv.ptr<!spirv.struct<(i32, i1, vector<2xi1>)>, Workgroup>

  // CHECK-LABEL: llvm.func @main
  spirv.func @main() "None" {
    // A bare scalar `bool` global loads directly, no `getelementptr` at all.
    // CHECK: %[[SCALAR_ADDR:.*]] = llvm.mlir.addressof @scalar_bool
    // CHECK: llvm.load %[[SCALAR_ADDR]] : !llvm.ptr<3> -> i1
    %v0 = spirv.mlir.addressof @scalar_bool : !spirv.ptr<i1, Workgroup>
    %l0 = spirv.Load "Workgroup" %v0 : i1

    %v1 = spirv.mlir.addressof @wg : !spirv.ptr<!spirv.struct<(i32, i1, vector<2xi1>)>, Workgroup>
    %c1 = spirv.Constant 1 : i32
    %c2 = spirv.Constant 2 : i32
    %c0 = spirv.Constant 0 : i32

    // The struct's own bare `bool` member (index 1) loads through an
    // ordinary struct-indexed `getelementptr`.
    // CHECK: %[[BOOL_GEP:.*]] = llvm.getelementptr %{{.*}}[%{{.*}}, 1]
    // CHECK: llvm.load %[[BOOL_GEP]] : !llvm.ptr<3> -> i1
    %ap = spirv.AccessChain %v1[%c1] : !spirv.ptr<!spirv.struct<(i32, i1, vector<2xi1>)>, Workgroup>, i32 -> !spirv.ptr<i1, Workgroup>
    %l1 = spirv.Load "Workgroup" %ap : i1

    // The struct's `bvec2` member (index 2), lane 0: a full-vector load
    // followed by `extractelement`, never a lane-indexing `getelementptr`.
    // CHECK: llvm.getelementptr %{{.*}}[%{{.*}}, 2]
    // CHECK: %[[VEC:.*]] = llvm.load %{{.*}} : !llvm.ptr -> vector<2xi1>
    // CHECK: llvm.extractelement %[[VEC]]
    %ap2 = spirv.AccessChain %v1[%c2, %c0] : !spirv.ptr<!spirv.struct<(i32, i1, vector<2xi1>)>, Workgroup>, i32, i32 -> !spirv.ptr<i1, Workgroup>
    %l2 = spirv.Load "Workgroup" %ap2 : i1

    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main
  spirv.ExecutionMode @main "LocalSize", 1, 1, 1
}
