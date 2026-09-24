// RUN: mlir-opt -convert-spirv-to-llvm %s | FileCheck %s

//===----------------------------------------------------------------------===//
// spirv.AccessChain
//===----------------------------------------------------------------------===//

// CHECK-LABEL: @access_chain
spirv.func @access_chain() "None" {
  // CHECK: %[[ONE:.*]] = llvm.mlir.constant(1 : i32) : i32
  %0 = spirv.Constant 1: i32
  %1 = spirv.Variable : !spirv.ptr<!spirv.struct<(f32, !spirv.array<4xf32>)>, Function>
  // CHECK: %[[ZERO:.*]] = llvm.mlir.constant(0 : i32) : i32
  // CHECK: llvm.getelementptr %{{.*}}[%[[ZERO]], 1, %[[ONE]]] : (!llvm.ptr, i32, i32) -> !llvm.ptr, !llvm.struct<packed (f32, array<4 x f32>)>
  %2 = spirv.AccessChain %1[%0, %0] : !spirv.ptr<!spirv.struct<(f32, !spirv.array<4xf32>)>, Function>, i32, i32 -> !spirv.ptr<f32, Function>
  spirv.Return
}

// CHECK-LABEL: @access_chain_array
spirv.func @access_chain_array(%arg0 : i32) "None" {
  %0 = spirv.Variable : !spirv.ptr<!spirv.array<4x!spirv.array<4xf32>>, Function>
  // CHECK: %[[ZERO:.*]] = llvm.mlir.constant(0 : i32) : i32
  // CHECK: llvm.getelementptr %{{.*}}[%[[ZERO]], %{{.*}}] : (!llvm.ptr, i32, i32) -> !llvm.ptr, !llvm.array<4 x array<4 x f32>>
  %1 = spirv.AccessChain %0[%arg0] : !spirv.ptr<!spirv.array<4x!spirv.array<4xf32>>, Function>, i32 -> !spirv.ptr<!spirv.array<4xf32>, Function>
  %2 = spirv.Load "Function" %1 ["Volatile"] : !spirv.array<4xf32>
  spirv.Return
}

// A zero-index access chain is a legal (if degenerate) SPIR-V access chain
// -- `Indexes` is variadic and may be empty, in which case the result is
// simply a pointer to the same pointee as the base. This must not crash
// while picking an index type for the leading "step through the pointer"
// GEP index, since there is no index operand to infer a type from.
// (Uses the generic op syntax since the pretty assembly format can't
// currently round-trip a zero-length `$indices` operand list.)
// CHECK-LABEL: @access_chain_zero_indices
spirv.func @access_chain_zero_indices() "None" {
  %0 = spirv.Variable : !spirv.ptr<f32, Function>
  // CHECK: %[[ZERO:.*]] = llvm.mlir.constant(0 : i32) : i32
  // CHECK: llvm.getelementptr %{{.*}}[%[[ZERO]]] : (!llvm.ptr, i32) -> !llvm.ptr, f32
  %1 = "spirv.AccessChain"(%0) : (!spirv.ptr<f32, Function>) -> !spirv.ptr<f32, Function>
  %2 = spirv.Load "Function" %1 ["Volatile"] : f32
  spirv.Return
}

//===----------------------------------------------------------------------===//
// spirv.InBoundsAccessChain
//===----------------------------------------------------------------------===//

// CHECK-LABEL: @in_bounds_access_chain
spirv.func @in_bounds_access_chain() "None" {
  // CHECK: %[[ONE:.*]] = llvm.mlir.constant(1 : i32) : i32
  %0 = spirv.Constant 1: i32
  %1 = spirv.Variable : !spirv.ptr<!spirv.struct<(f32, !spirv.array<4xf32>)>, Function>
  // CHECK: %[[ZERO:.*]] = llvm.mlir.constant(0 : i32) : i32
  // CHECK: llvm.getelementptr inbounds %{{.*}}[%[[ZERO]], 1, %[[ONE]]] : (!llvm.ptr, i32, i32) -> !llvm.ptr, !llvm.struct<packed (f32, array<4 x f32>)>
  %2 = spirv.InBoundsAccessChain %1[%0, %0] : !spirv.ptr<!spirv.struct<(f32, !spirv.array<4xf32>)>, Function>, i32, i32 -> !spirv.ptr<f32, Function>
  spirv.Return
}

// A zero-index in-bounds access chain is a legal (if degenerate) SPIR-V
// access chain, exactly like its `spirv.AccessChain` sibling above -- must
// not crash while picking an index type for the leading "step through the
// pointer" GEP index.
// CHECK-LABEL: @in_bounds_access_chain_zero_indices
spirv.func @in_bounds_access_chain_zero_indices() "None" {
  %0 = spirv.Variable : !spirv.ptr<f32, Function>
  // CHECK: %[[ZERO:.*]] = llvm.mlir.constant(0 : i32) : i32
  // CHECK: llvm.getelementptr inbounds %{{.*}}[%[[ZERO]]] : (!llvm.ptr, i32) -> !llvm.ptr, f32
  %1 = "spirv.InBoundsAccessChain"(%0) : (!spirv.ptr<f32, Function>) -> !spirv.ptr<f32, Function>
  %2 = spirv.Load "Function" %1 ["Volatile"] : f32
  spirv.Return
}

//===----------------------------------------------------------------------===//
// spirv.GlobalVariable and spirv.mlir.addressof
//===----------------------------------------------------------------------===//

spirv.module Logical GLSL450 {
  // CHECK: llvm.mlir.global external constant @var() {addr_space = 0 : i32} : f32
  spirv.GlobalVariable @var : !spirv.ptr<f32, Input>
}

spirv.module Logical GLSL450 {
  //       CHECK: llvm.mlir.global private @struct() {addr_space = 0 : i32} : !llvm.struct<packed (f32, array<10 x f32>)>
  // CHECK-LABEL: @func
  //       CHECK:   llvm.mlir.addressof @struct : !llvm.ptr
  spirv.GlobalVariable @struct : !spirv.ptr<!spirv.struct<(f32, !spirv.array<10xf32>)>, Private>
  spirv.func @func() "None" {
    %0 = spirv.mlir.addressof @struct : !spirv.ptr<!spirv.struct<(f32, !spirv.array<10xf32>)>, Private>
    spirv.Return
  }
}

spirv.module Logical GLSL450 {
  //       CHECK: llvm.mlir.global external @bar_descriptor_set0_binding0() {addr_space = 0 : i32} : i32
  // CHECK-LABEL: @foo
  //       CHECK:   llvm.mlir.addressof @bar_descriptor_set0_binding0 : !llvm.ptr
  spirv.GlobalVariable @bar bind(0, 0) : !spirv.ptr<i32, StorageBuffer>
  spirv.func @foo() "None" {
    %0 = spirv.mlir.addressof @bar : !spirv.ptr<i32, StorageBuffer>
    spirv.Return
  }
}

spirv.module @name Logical GLSL450 {
  //       CHECK: llvm.mlir.global external @name_bar_descriptor_set0_binding0() {addr_space = 0 : i32} : i32
  // CHECK-LABEL: @foo
  //       CHECK:   llvm.mlir.addressof @name_bar_descriptor_set0_binding0 : !llvm.ptr
  spirv.GlobalVariable @bar bind(0, 0) : !spirv.ptr<i32, StorageBuffer>
  spirv.func @foo() "None" {
    %0 = spirv.mlir.addressof @bar : !spirv.ptr<i32, StorageBuffer>
    spirv.Return
  }
}

spirv.module Logical GLSL450 {
  // CHECK: llvm.mlir.global external @bar() {addr_space = 0 : i32, location = 1 : i32} : i32
  // CHECK-LABEL: @foo
  spirv.GlobalVariable @bar {location = 1 : i32} : !spirv.ptr<i32, Output>
  spirv.func @foo() "None" {
    %0 = spirv.mlir.addressof @bar : !spirv.ptr<i32, Output>
    spirv.Return
  }
}

spirv.module Logical GLSL450 {
  // CHECK: llvm.mlir.global external constant @bar() {addr_space = 0 : i32, location = 3 : i32} : f32
  // CHECK-LABEL: @foo
  spirv.GlobalVariable @bar {descriptor_set = 0 : i32, location = 3 : i32} : !spirv.ptr<f32, UniformConstant>
  spirv.func @foo() "None" {
    %0 = spirv.mlir.addressof @bar : !spirv.ptr<f32, UniformConstant>
    spirv.Return
  }
}

spirv.module Logical GLSL450 {
  //       CHECK: llvm.mlir.global external constant @image_descriptor_set0_binding0() {addr_space = 0 : i32} : !llvm.target<"spirv.Image", f32, 5, 0, 0, 0, 2, 1>
  // CHECK-LABEL: @image_resource
  //       CHECK:   %[[IMG:.*]] = llvm.mlir.addressof @image_descriptor_set0_binding0 : !llvm.ptr
  //       CHECK:   llvm.load %[[IMG]] : !llvm.ptr -> !llvm.target<"spirv.Image", f32, 5, 0, 0, 0, 2, 1>
  spirv.GlobalVariable @image bind(0, 0) : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
  spirv.func @image_resource() "None" {
    %0 = spirv.mlir.addressof @image : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>
    spirv.Return
  }
}

spirv.module Logical GLSL450 {
  //       CHECK: llvm.mlir.global external constant @sampler_descriptor_set0_binding1() {addr_space = 0 : i32} : !llvm.target<"spirv.Sampler">
  // CHECK-LABEL: @sampler_resource
  //       CHECK:   %[[SMP:.*]] = llvm.mlir.addressof @sampler_descriptor_set0_binding1 : !llvm.ptr
  //       CHECK:   llvm.load %[[SMP]] : !llvm.ptr -> !llvm.target<"spirv.Sampler">
  spirv.GlobalVariable @sampler bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @sampler_resource() "None" {
    %0 = spirv.mlir.addressof @sampler : !spirv.ptr<!spirv.sampler, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.sampler
    spirv.Return
  }
}

//===----------------------------------------------------------------------===//
// spirv.Load
//===----------------------------------------------------------------------===//

// CHECK-LABEL: @load
spirv.func @load() "None" {
  %0 = spirv.Variable : !spirv.ptr<f32, Function>
  //  CHECK: llvm.load %{{.*}} : !llvm.ptr -> f32
  %1 = spirv.Load "Function" %0 : f32
  spirv.Return
}

// CHECK-LABEL: @load_none
spirv.func @load_none() "None" {
  %0 = spirv.Variable : !spirv.ptr<f32, Function>
  //  CHECK: llvm.load %{{.*}} : !llvm.ptr -> f32
  %1 = spirv.Load "Function" %0 ["None"] : f32
  spirv.Return
}

// CHECK-LABEL: @load_with_alignment
spirv.func @load_with_alignment() "None" {
  %0 = spirv.Variable : !spirv.ptr<f32, Function>
  // CHECK: llvm.load %{{.*}} <alignment = 4> : !llvm.ptr -> f32
  %1 = spirv.Load "Function" %0 ["Aligned", 4] : f32
  spirv.Return
}

// CHECK-LABEL: @load_volatile
spirv.func @load_volatile() "None" {
  %0 = spirv.Variable : !spirv.ptr<f32, Function>
  // CHECK: llvm.load volatile %{{.*}} : !llvm.ptr -> f32
  %1 = spirv.Load "Function" %0 ["Volatile"] : f32
  spirv.Return
}

// CHECK-LABEL: @load_nontemporal
spirv.func @load_nontemporal() "None" {
  %0 = spirv.Variable : !spirv.ptr<f32, Function>
  // CHECK: llvm.load %{{.*}} <nontemporal> : !llvm.ptr -> f32
  %1 = spirv.Load "Function" %0 ["Nontemporal"] : f32
  spirv.Return
}

//===----------------------------------------------------------------------===//
// spirv.Store
//===----------------------------------------------------------------------===//

// CHECK-LABEL: @store
spirv.func @store(%arg0 : f32) "None" {
  %0 = spirv.Variable : !spirv.ptr<f32, Function>
  // CHECK: llvm.store %{{.*}}, %{{.*}} : f32, !llvm.ptr
  spirv.Store "Function" %0, %arg0 : f32
  spirv.Return
}

// CHECK-LABEL: @store_composite
spirv.func @store_composite(%arg0 : !spirv.struct<(f64)>) "None" {
  %0 = spirv.Variable : !spirv.ptr<!spirv.struct<(f64)>, Function>
  // CHECK: llvm.store %{{.*}}, %{{.*}} : !llvm.struct<packed (f64)>, !llvm.ptr
  spirv.Store "Function" %0, %arg0 : !spirv.struct<(f64)>
  spirv.Return
}

// CHECK-LABEL: @store_with_alignment
spirv.func @store_with_alignment(%arg0 : f32) "None" {
  %0 = spirv.Variable : !spirv.ptr<f32, Function>
  // CHECK: llvm.store %{{.*}}, %{{.*}} <alignment = 4> : f32, !llvm.ptr
  spirv.Store "Function" %0, %arg0 ["Aligned", 4] : f32
  spirv.Return
}

// CHECK-LABEL: @store_volatile
spirv.func @store_volatile(%arg0 : f32) "None" {
  %0 = spirv.Variable : !spirv.ptr<f32, Function>
  // CHECK: llvm.store volatile %{{.*}}, %{{.*}} : f32, !llvm.ptr
  spirv.Store "Function" %0, %arg0 ["Volatile"] : f32
  spirv.Return
}

// CHECK-LABEL: @store_nontemporal
spirv.func @store_nontemporal(%arg0 : f32) "None" {
  %0 = spirv.Variable : !spirv.ptr<f32, Function>
  // CHECK: llvm.store %{{.*}}, %{{.*}} <nontemporal> : f32, !llvm.ptr
  spirv.Store "Function" %0, %arg0 ["Nontemporal"] : f32
  spirv.Return
}

// Checks a `spirv.Store`/`spirv.Load` combining two independent
// `MemoryAccess` bits at once (`Volatile|Nontemporal`) -- a real
// dEQP-VK.graphicsfuzz.spv-stable-pillars-volatile-nontemporal-store SPIR-V
// shape. `MemoryAccess` is a bit-enum attribute whose flags are
// independently combinable, so this must not be confused with the single
// `Aligned`/`Volatile`/`Nontemporal`-only cases above: this previously fell
// through to the default "unsupported memory access" case and failed
// conversion outright, since a combined value is not equal to any single
// enumerant.

// CHECK-LABEL: @store_volatile_nontemporal
spirv.func @store_volatile_nontemporal(%arg0 : f32) "None" {
  %0 = spirv.Variable : !spirv.ptr<f32, Function>
  // CHECK: llvm.store volatile %{{.*}}, %{{.*}} <nontemporal> : f32, !llvm.ptr
  spirv.Store "Function" %0, %arg0 ["Volatile|Nontemporal"] : f32
  spirv.Return
}

// CHECK-LABEL: @load_volatile_nontemporal
spirv.func @load_volatile_nontemporal() "None" {
  %0 = spirv.Variable : !spirv.ptr<f32, Function>
  // CHECK: llvm.load volatile %{{.*}} <nontemporal> : !llvm.ptr -> f32
  %1 = spirv.Load "Function" %0 ["Volatile|Nontemporal"] : f32
  spirv.Return
}

// Checks a combination that also carries `Aligned` (so all three
// currently-supported bits are set at once), confirming the alignment
// operand is still parsed and honored alongside the other two bits.

// CHECK-LABEL: @store_volatile_nontemporal_aligned
spirv.func @store_volatile_nontemporal_aligned(%arg0 : f32) "None" {
  %0 = spirv.Variable : !spirv.ptr<f32, Function>
  // CHECK: llvm.store volatile %{{.*}}, %{{.*}} <alignment = 4, nontemporal> : f32, !llvm.ptr
  spirv.Store "Function" %0, %arg0 ["Volatile|Aligned|Nontemporal", 4] : f32
  spirv.Return
}

//===----------------------------------------------------------------------===//
// spirv.Variable
//===----------------------------------------------------------------------===//

// CHECK-LABEL: @variable_scalar
spirv.func @variable_scalar() "None" {
  // CHECK: %[[SIZE1:.*]] = llvm.mlir.constant(1 : i32) : i32
  // CHECK: llvm.alloca %[[SIZE1]] x f32 : (i32) -> !llvm.ptr
  %0 = spirv.Variable : !spirv.ptr<f32, Function>
  // CHECK: %[[SIZE2:.*]] = llvm.mlir.constant(1 : i32) : i32
  // CHECK: llvm.alloca %[[SIZE2]] x i8 : (i32) -> !llvm.ptr
  %1 = spirv.Variable : !spirv.ptr<i8, Function>
  spirv.Return
}

// CHECK-LABEL: @variable_scalar_with_initialization
spirv.func @variable_scalar_with_initialization() "None" {
  // CHECK: %[[VALUE:.*]] = llvm.mlir.constant(0 : i64) : i64
  // CHECK: %[[SIZE:.*]] = llvm.mlir.constant(1 : i32) : i32
  // CHECK: %[[ALLOCATED:.*]] = llvm.alloca %[[SIZE]] x i64 : (i32) -> !llvm.ptr
  // CHECK: llvm.store %[[VALUE]], %[[ALLOCATED]] : i64, !llvm.ptr
  %c = spirv.Constant 0 : i64
  %0 = spirv.Variable init(%c) : !spirv.ptr<i64, Function>
  spirv.Return
}

// CHECK-LABEL: @variable_vector
spirv.func @variable_vector() "None" {
  // CHECK: %[[SIZE:.*]] = llvm.mlir.constant(1 : i32) : i32
  // CHECK: llvm.alloca  %[[SIZE]] x vector<3xf32> : (i32) -> !llvm.ptr
  %0 = spirv.Variable : !spirv.ptr<vector<3xf32>, Function>
  spirv.Return
}

// CHECK-LABEL: @variable_vector_with_initialization
spirv.func @variable_vector_with_initialization() "None" {
  // CHECK: %[[VALUE:.*]] = llvm.mlir.constant(dense<false> : vector<3xi1>) : vector<3xi1>
  // CHECK: %[[SIZE:.*]] = llvm.mlir.constant(1 : i32) : i32
  // CHECK: %[[ALLOCATED:.*]] = llvm.alloca %[[SIZE]] x vector<3xi1> : (i32) -> !llvm.ptr
  // CHECK: llvm.store %[[VALUE]], %[[ALLOCATED]] : vector<3xi1>, !llvm.ptr
  %c = spirv.Constant dense<false> : vector<3xi1>
  %0 = spirv.Variable init(%c) : !spirv.ptr<vector<3xi1>, Function>
  spirv.Return
}

// CHECK-LABEL: @variable_array
spirv.func @variable_array() "None" {
  // CHECK: %[[SIZE:.*]] = llvm.mlir.constant(1 : i32) : i32
  // CHECK: llvm.alloca %[[SIZE]] x !llvm.array<10 x i32> : (i32) -> !llvm.ptr
  %0 = spirv.Variable : !spirv.ptr<!spirv.array<10 x i32>, Function>
  spirv.Return
}

// `initial_value` (roadmap L124(b)): a plain constant Initializer lowers
// straight to `llvm.mlir.global`'s own scalar `value` attribute -- unlike
// every other `spirv.GlobalVariable` case above, whose global carries no
// initializer at all. A `ui32`-typed constant (SPIR-V keeps signedness on
// the type, not the value) round-trips through the same sign-stripping
// `spirv.Constant` itself already goes through, becoming a plain signless
// `i32` value here.
spirv.module Logical GLSL450 {
  // CHECK: llvm.mlir.global private @count(0 : i32) {addr_space = 0 : i32} : i32
  spirv.GlobalVariable @count initial_value(0 : i32) : !spirv.ptr<i32, Private>
}
