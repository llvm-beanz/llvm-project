// RUN: mlir-opt -split-input-file -verify-diagnostics %s | FileCheck %s

//===----------------------------------------------------------------------===//
// spirv.Undef
//===----------------------------------------------------------------------===//

func.func @undef() -> () {
  // CHECK: %{{.*}} = spirv.Undef : f32
  %0 = spirv.Undef : f32
  // CHECK: %{{.*}} = spirv.Undef : vector<4xf32>
  %1 = spirv.Undef : vector<4xf32>
  spirv.Return
}

// -----

func.func @undef() -> () {
  // expected-error @+1{{expected non-function type}}
  %0 = spirv.Undef :
  spirv.Return
}

// -----

func.func @undef() -> () {
  // expected-error @+1{{expected ':'}}
  %0 = spirv.Undef
  spirv.Return
}

// -----

func.func @assume_true(%arg : i1) -> () {
  // CHECK: spirv.KHR.AssumeTrue %{{.*}}
  spirv.KHR.AssumeTrue %arg
  spirv.Return
}

// -----

func.func @assume_true(%arg : f32) -> () {
  // expected-error @+2{{use of value '%arg' expects different type than prior uses: 'i1' vs 'f32'}}
  // expected-note @-2 {{prior use here}}
  spirv.KHR.AssumeTrue %arg
  spirv.Return
}

// -----

//===----------------------------------------------------------------------===//
// spirv.KHR.Expect
//===----------------------------------------------------------------------===//

func.func @expect_scalar_int(%val : i32, %expected : i32) -> i32 {
  // CHECK: %{{.*}} = spirv.KHR.Expect %{{.*}}, %{{.*}} : i32
  %0 = spirv.KHR.Expect %val, %expected : i32
  spirv.ReturnValue %0 : i32
}

// -----

func.func @expect_scalar_bool(%val : i1, %expected : i1) -> i1 {
  // CHECK: %{{.*}} = spirv.KHR.Expect %{{.*}}, %{{.*}} : i1
  %0 = spirv.KHR.Expect %val, %expected : i1
  spirv.ReturnValue %0 : i1
}

// -----

func.func @expect_vector_int(%val : vector<4xi32>, %expected : vector<4xi32>) -> vector<4xi32> {
  // CHECK: %{{.*}} = spirv.KHR.Expect %{{.*}}, %{{.*}} : vector<4xi32>
  %0 = spirv.KHR.Expect %val, %expected : vector<4xi32>
  spirv.ReturnValue %0 : vector<4xi32>
}

// -----

func.func @expect_vector_bool(%val : vector<4xi1>, %expected : vector<4xi1>) -> vector<4xi1> {
  // CHECK: %{{.*}} = spirv.KHR.Expect %{{.*}}, %{{.*}} : vector<4xi1>
  %0 = spirv.KHR.Expect %val, %expected : vector<4xi1>
  spirv.ReturnValue %0 : vector<4xi1>
}

// -----

func.func @expect_type_mismatch(%val : i32, %expected : i64) -> i32 {
  // expected-error @+1 {{op failed to verify that all of {value, expectedValue, result} have same type}}
  %0 = "spirv.KHR.Expect"(%val, %expected) : (i32, i64) -> i32
  spirv.ReturnValue %0 : i32
}

// -----

func.func @expect_float_invalid(%val : f32, %expected : f32) -> f32 {
  // expected-error @+1 {{op operand #0 must be}}
  %0 = "spirv.KHR.Expect"(%val, %expected) : (f32, f32) -> f32
  spirv.ReturnValue %0 : f32
}

// -----

//===----------------------------------------------------------------------===//
// spirv.CopyObject
//===----------------------------------------------------------------------===//

func.func @copy_object_scalar(%arg : f32) -> f32 {
  // CHECK: %{{.*}} = spirv.CopyObject %{{.*}} : f32
  %0 = spirv.CopyObject %arg : f32
  spirv.ReturnValue %0 : f32
}

// -----

func.func @copy_object_vector(%arg : vector<4xi32>) -> vector<4xi32> {
  // CHECK: %{{.*}} = spirv.CopyObject %{{.*}} : vector<4xi32>
  %0 = spirv.CopyObject %arg : vector<4xi32>
  spirv.ReturnValue %0 : vector<4xi32>
}

// -----

func.func @copy_object_type_mismatch(%arg : i32) -> i64 {
  // expected-error @+1 {{op failed to verify that all of {operand, result} have same type}}
  %0 = "spirv.CopyObject"(%arg) : (i32) -> i64
  spirv.ReturnValue %0 : i64
}

// -----

//===----------------------------------------------------------------------===//
// spirv.CopyLogical
//===----------------------------------------------------------------------===//

func.func @copy_logical_identical_structs(
    %arg : !spirv.struct<(i32, i32)>) -> !spirv.struct<(i32, i32)> {
  // CHECK: %{{.*}} = spirv.CopyLogical %{{.*}} : !spirv.struct<(i32, i32)> to !spirv.struct<(i32, i32)>
  %0 = spirv.CopyLogical %arg : !spirv.struct<(i32, i32)> to !spirv.struct<(i32, i32)>
  spirv.ReturnValue %0 : !spirv.struct<(i32, i32)>
}

// -----

// Two differently-identified struct types with the same member types are
// logically compatible -- exactly the shape a real deduplicated/re-emitted
// SPIR-V module produces for what is conceptually "the same" struct type
// under two different IDs.
func.func @copy_logical_distinct_identified_structs(
    %arg : !spirv.struct<a_struct, (i32, !spirv.array<2 x i32>)>)
    -> !spirv.struct<b_struct, (i32, !spirv.array<2 x i32>)> {
  // CHECK: %{{.*}} = spirv.CopyLogical %{{.*}} : !spirv.struct<a_struct, (i32, !spirv.array<2 x i32>)> to !spirv.struct<b_struct, (i32, !spirv.array<2 x i32>)>
  %0 = spirv.CopyLogical %arg
      : !spirv.struct<a_struct, (i32, !spirv.array<2 x i32>)>
      to !spirv.struct<b_struct, (i32, !spirv.array<2 x i32>)>
  spirv.ReturnValue %0 : !spirv.struct<b_struct, (i32, !spirv.array<2 x i32>)>
}

// -----

func.func @copy_logical_member_count_mismatch(
    %arg : !spirv.struct<(i32, i32)>) -> !spirv.struct<(i32)> {
  // expected-error @+1 {{op operand type '!spirv.struct<(i32, i32)>' and result type '!spirv.struct<(i32)>' are not logically compatible}}
  %0 = spirv.CopyLogical %arg : !spirv.struct<(i32, i32)> to !spirv.struct<(i32)>
  spirv.ReturnValue %0 : !spirv.struct<(i32)>
}

// -----

func.func @copy_logical_member_type_mismatch(
    %arg : !spirv.struct<(i32, i32)>) -> !spirv.struct<(i32, f32)> {
  // expected-error @+1 {{op operand type '!spirv.struct<(i32, i32)>' and result type '!spirv.struct<(i32, f32)>' are not logically compatible}}
  %0 = spirv.CopyLogical %arg : !spirv.struct<(i32, i32)> to !spirv.struct<(i32, f32)>
  spirv.ReturnValue %0 : !spirv.struct<(i32, f32)>
}

