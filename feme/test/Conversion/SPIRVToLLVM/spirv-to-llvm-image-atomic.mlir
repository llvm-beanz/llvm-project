// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap H8u/R39: `spirv.ImageTexelPointer` forms a texel address the same
// `llvm.spv.resource.getpointer` intrinsic `spirv.ImageRead`/`spirv.
// ImageWrite` already use (see spirv-to-llvm-image-access.mlir), so a
// following `spirv.Atomic*` op converts into an ordinary `llvm.atomicrmw`
// against that pointer -- MLIR's own upstream `spirv` -> `llvm` conversion
// has no pattern for any `Atomic*` op at all (confirmed by grep), so this
// converter supplies its own for every RMW-shaped one plus
// `AtomicCompareExchange`.

// CHECK-LABEL: llvm.func @atomic_add
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}}) : (!llvm.target<"spirv.Image", i32, 1, 0, 0, 0, 2, 24>, vector<2xi32>) -> !llvm.ptr
// CHECK: %[[OLD:.*]] = llvm.atomicrmw add %[[PTR]], %{{.*}} seq_cst : !llvm.ptr, i32
// CHECK: llvm.return %[[OLD]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, R32i>, UniformConstant>
  spirv.func @atomic_add(%coord : vector<2xi32>, %value : i32) -> i32 "None" {
    %zero = spirv.Constant 0 : i32
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, R32i>, UniformConstant>
    %1 = spirv.ImageTexelPointer %0, %coord, %zero : !spirv.ptr<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, R32i>, UniformConstant>, vector<2xi32>, i32 -> !spirv.ptr<i32, Image>
    %2 = spirv.AtomicIAdd <Device> <None> %1, %value : !spirv.ptr<i32, Image>
    spirv.ReturnValue %2 : i32
  }
}

// -----

// Every other RMW-shaped `Atomic*` op this converter supports, on the same
// texel pointer -- confirms each picks the matching `llvm.atomicrmw` kind.

// CHECK-LABEL: llvm.func @atomic_rmw_kinds
// CHECK: llvm.atomicrmw sub %{{.*}}, %{{.*}} seq_cst : !llvm.ptr, i32
// CHECK: llvm.atomicrmw _and %{{.*}}, %{{.*}} seq_cst : !llvm.ptr, i32
// CHECK: llvm.atomicrmw _or %{{.*}}, %{{.*}} seq_cst : !llvm.ptr, i32
// CHECK: llvm.atomicrmw _xor %{{.*}}, %{{.*}} seq_cst : !llvm.ptr, i32
// CHECK: llvm.atomicrmw max %{{.*}}, %{{.*}} seq_cst : !llvm.ptr, i32
// CHECK: llvm.atomicrmw min %{{.*}}, %{{.*}} seq_cst : !llvm.ptr, i32
// CHECK: llvm.atomicrmw umax %{{.*}}, %{{.*}} seq_cst : !llvm.ptr, i32
// CHECK: llvm.atomicrmw umin %{{.*}}, %{{.*}} seq_cst : !llvm.ptr, i32
// CHECK: llvm.atomicrmw xchg %{{.*}}, %{{.*}} seq_cst : !llvm.ptr, i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, R32i>, UniformConstant>
  spirv.func @atomic_rmw_kinds(%coord : vector<2xi32>, %value : i32) -> i32 "None" {
    %zero = spirv.Constant 0 : i32
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, R32i>, UniformConstant>
    %1 = spirv.ImageTexelPointer %0, %coord, %zero : !spirv.ptr<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, R32i>, UniformConstant>, vector<2xi32>, i32 -> !spirv.ptr<i32, Image>
    %2 = spirv.AtomicISub <Device> <None> %1, %value : !spirv.ptr<i32, Image>
    %3 = spirv.AtomicAnd <Device> <None> %1, %value : !spirv.ptr<i32, Image>
    %4 = spirv.AtomicOr <Device> <None> %1, %value : !spirv.ptr<i32, Image>
    %5 = spirv.AtomicXor <Device> <None> %1, %value : !spirv.ptr<i32, Image>
    %6 = spirv.AtomicSMax <Device> <None> %1, %value : !spirv.ptr<i32, Image>
    %7 = spirv.AtomicSMin <Device> <None> %1, %value : !spirv.ptr<i32, Image>
    %8 = spirv.AtomicUMax <Device> <None> %1, %value : !spirv.ptr<i32, Image>
    %9 = spirv.AtomicUMin <Device> <None> %1, %value : !spirv.ptr<i32, Image>
    %10 = spirv.AtomicExchange <Device> <None> %1, %value : !spirv.ptr<i32, Image>
    spirv.ReturnValue %10 : i32
  }
}

// -----

// `spirv.AtomicCompareExchange` converts into `llvm.cmpxchg` plus an
// `extractvalue` of its first (old-value) result element -- SPIR-V's own
// result is always the pre-swap value, whether or not the comparison
// succeeded.

// CHECK-LABEL: llvm.func @atomic_cmpxchg
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"
// CHECK: %[[PAIR:.*]] = llvm.cmpxchg %[[PTR]], %{{.*}}, %{{.*}} seq_cst seq_cst : !llvm.ptr, i32
// CHECK: %[[OLD:.*]] = llvm.extractvalue %[[PAIR]][0] : !llvm.struct<(i32, i1)>
// CHECK: llvm.return %[[OLD]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, R32i>, UniformConstant>
  spirv.func @atomic_cmpxchg(%coord : vector<2xi32>, %value : i32, %comparator : i32) -> i32 "None" {
    %zero = spirv.Constant 0 : i32
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, R32i>, UniformConstant>
    %1 = spirv.ImageTexelPointer %0, %coord, %zero : !spirv.ptr<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, R32i>, UniformConstant>, vector<2xi32>, i32 -> !spirv.ptr<i32, Image>
    %2 = spirv.AtomicCompareExchange <Device> <None> <None> %1, %value, %comparator : !spirv.ptr<i32, Image>
    spirv.ReturnValue %2 : i32
  }
}

// -----

// Roadmap H19g's own multisampled-2D `Sample` widening applies to
// `ImageTexelPointer`'s coordinate the same way it already applies to
// `ImageRead`/`ImageWrite` (spirv-to-llvm-image-access-multisample.mlir).

// CHECK-LABEL: llvm.func @atomic_add_ms
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : vector<3xi32>
// CHECK-DAG: %[[X:.*]] = llvm.extractelement %{{.*}}[%{{.*}} : i64] : vector<2xi32>
// CHECK-DAG: %[[V0:.*]] = llvm.insertelement %[[X]], %[[POISON]][%{{.*}} : i64] : vector<3xi32>
// CHECK-DAG: %[[Y:.*]] = llvm.extractelement %{{.*}}[%{{.*}} : i64] : vector<2xi32>
// CHECK-DAG: %[[V1:.*]] = llvm.insertelement %[[Y]], %[[V0]][%{{.*}} : i64] : vector<3xi32>
// CHECK: %[[COORD:.*]] = llvm.insertelement %{{.*}}, %[[V1]][%{{.*}} : i64] : vector<3xi32>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %[[COORD]]) : (!llvm.target<"spirv.Image", i32, 1, 0, 0, 1, 2, 24>, vector<3xi32>) -> !llvm.ptr
// CHECK: llvm.atomicrmw add %[[PTR]], %{{.*}} seq_cst : !llvm.ptr, i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, MultiSampled, NoSampler, R32i>, UniformConstant>
  spirv.func @atomic_add_ms(%coord : vector<2xi32>, %sample : i32, %value : i32) -> i32 "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, MultiSampled, NoSampler, R32i>, UniformConstant>
    %1 = spirv.ImageTexelPointer %0, %coord, %sample : !spirv.ptr<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, MultiSampled, NoSampler, R32i>, UniformConstant>, vector<2xi32>, i32 -> !spirv.ptr<i32, Image>
    %2 = spirv.AtomicIAdd <Device> <None> %1, %value : !spirv.ptr<i32, Image>
    spirv.ReturnValue %2 : i32
  }
}
