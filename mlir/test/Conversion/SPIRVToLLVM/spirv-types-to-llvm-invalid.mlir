// RUN: mlir-opt %s -convert-spirv-to-llvm -verify-diagnostics -split-input-file

// expected-error@+1 {{failed to legalize operation 'spirv.func' that was explicitly marked illegal}}
spirv.func @array_with_unnatural_stride(%arg: !spirv.array<4 x f32, stride=8>) -> () "None" {
  spirv.Return
}

// -----

// A 3-component vector's own Vulkan base alignment (16 bytes) is what its
// natural array stride must match -- not its own compact size (12 bytes),
// which no real SPIR-V producer ever actually uses as an `ArrayStride`.
// expected-error@+1 {{failed to legalize operation 'spirv.func' that was explicitly marked illegal}}
spirv.func @array_with_unnatural_vector3_stride(%arg: !spirv.array<4 x vector<3xf32>, stride=12>) -> () "None" {
  spirv.Return
}

// -----

// expected-error@+1 {{failed to legalize operation 'spirv.func' that was explicitly marked illegal}}
spirv.func @struct_array_with_unnatural_stride(%arg: !spirv.struct<(!spirv.array<4 x f32, stride=8>)>) -> () "None" {
  spirv.Return
}

// -----

// expected-error@+1 {{failed to legalize operation 'spirv.func' that was explicitly marked illegal}}
spirv.func @struct_with_unnatural_offset(%arg: !spirv.struct<(i32[0], i32[8])>) -> () "None" {
  spirv.Return
}

// -----

// expected-error@+1 {{failed to legalize operation 'spirv.func' that was explicitly marked illegal}}
spirv.func @array_of_unconvertible_element(%arg: !spirv.array<2 x !spirv.matrix<2 x vector<2xf32>>>) -> () "None" {
  spirv.Return
}

// -----

// expected-error@+1 {{failed to legalize operation 'spirv.func' that was explicitly marked illegal}}
spirv.func @runtime_array_of_unconvertible_element(%arg: !spirv.rtarray<!spirv.matrix<2 x vector<2xf32>>>) -> () "None" {
  spirv.Return
}

// -----

// expected-error@+1 {{failed to legalize operation 'spirv.func' that was explicitly marked illegal}}
spirv.func @struct_with_decorations(%arg: !spirv.struct<(f32 [RelaxedPrecision])>) -> () "None" {
  spirv.Return
}
