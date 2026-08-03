// RUN: feme-translate --no-implicit-module --serialize-spirv %s -o %t.spv
// RUN: feme-translate --import-spirv %t.spv | \
// RUN:   feme-translate --no-implicit-module --spirv-to-llvmir - -o %t.ll
// RUN: feme-translate --llvm-backend --target-triple=spirv-unknown-vulkan-compute \
// RUN:   %t.ll -o %t.roundtrip.spv
// RUN: feme-translate --import-spirv %t.roundtrip.spv | FileCheck %s
//
// REQUIRES: spirv-registered-target

// The counterpart to spirv-backend-null-pipeline.mlir for a shader that
// actually does something: a compute shader that reads its dispatch thread
// id and copies one texel of an `RWBuffer` onto itself. Every stage that
// only exists because of that (builtin input variables, resource handles,
// image accesses) is a `llvm.spv.*` target intrinsic in the intermediate
// LLVM IR, which is what makes the module something LLVM's SPIRV backend can
// lower -- see the "SPIR-V -> MLIR `llvm` dialect -> LLVM IR" section of
// feme/docs/Design.md.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, ImageBuffer], []> {
  spirv.GlobalVariable @gid built_in("GlobalInvocationId") : !spirv.ptr<vector<3xi32>, Input>
  spirv.GlobalVariable @buf bind(0, 1) : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
  spirv.func @main() -> () "None" {
    %0 = spirv.mlir.addressof @gid : !spirv.ptr<vector<3xi32>, Input>
    %1 = spirv.Load "Input" %0 : vector<3xi32>
    %2 = spirv.CompositeExtract %1[0 : i32] : vector<3xi32>
    %3 = spirv.mlir.addressof @buf : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
    %4 = spirv.Load "UniformConstant" %3 : !spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>
    %5 = spirv.ImageRead %4, %2 : !spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, i32 -> vector<4xf32>
    spirv.ImageWrite %4, %2, %5 : !spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, i32, vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main, @gid, @buf
  spirv.ExecutionMode @main "LocalSize", 8, 8, 1
}

// The re-imported binary is a real GLCompute entry point again -- with its
// workgroup size, its builtin input variable, and a bound image it reads and
// writes -- rather than the exported plain function a module with no
// `hlsl.shader` attribute would have produced.
// CHECK: spirv.GlobalVariable @{{.*}} built_in("GlobalInvocationId")
// CHECK: spirv.GlobalVariable @{{.*}} bind(0, 1) : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
// CHECK: spirv.func @main()
// CHECK: spirv.ImageRead
// CHECK: spirv.ImageWrite
// CHECK: spirv.EntryPoint "GLCompute" @main
// CHECK: spirv.ExecutionMode @main "LocalSize", 8, 8, 1
