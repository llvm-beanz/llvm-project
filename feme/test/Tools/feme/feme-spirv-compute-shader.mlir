// REQUIRES: spirv-registered-target
// RUN: feme-translate --no-implicit-module --serialize-spirv %s -o %t.spv
// RUN: feme --from=spirv --to=spirv %t.spv -o %t.roundtrip.spv
// RUN: feme-translate --import-spirv %t.roundtrip.spv | FileCheck %s

// The `feme` CLI counterpart to test/Target/spirv-backend-compute-shader.mlir:
// the same compute shader, driven through feme::Driver in one step rather
// than one feme-translate stage at a time. Unlike the trivial null pipeline
// (feme-spirv-null-pipeline.mlir), this shader reads a builtin input variable
// and reads and writes a bound resource, so it only survives the trip because
// the SPIR-V -> LLVM IR translation emits `llvm.spv.*` target intrinsics for
// them; and the re-emitted module keeps the Vulkan compute environment the
// input named, rather than falling back to feme::Driver's kernel-flavored
// default for `--to=spirv`.

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

// CHECK: spirv.GlobalVariable @{{.*}} built_in("GlobalInvocationId")
// CHECK: spirv.GlobalVariable @{{.*}} bind(0, 1)
// CHECK: spirv.func @main()
// CHECK: spirv.ImageRead
// CHECK: spirv.ImageWrite
// CHECK: spirv.EntryPoint "GLCompute" @main
// CHECK: spirv.ExecutionMode @main "LocalSize", 8, 8, 1
