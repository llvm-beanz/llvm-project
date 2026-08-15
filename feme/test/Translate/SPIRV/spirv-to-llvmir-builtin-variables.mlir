// RUN: feme-translate --no-implicit-module --spirv-to-llvmir %s | FileCheck %s

// Runs the whole `spirv` dialect -> LLVM IR translation on a compute shader
// that reads a builtin input variable, and checks that the result is IR
// LLVM's SPIRV backend understands: an entry point named by `hlsl.shader`
// (and, for FeMe's own use, `feme.shader.stage`), with its workgroup size in
// `hlsl.numthreads`, reading its dispatch thread id through
// `llvm.spv.thread.id`. See the "SPIR-V -> MLIR `llvm` dialect ->
// LLVM IR" section of feme/docs/Design.md.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @gid built_in("GlobalInvocationId") : !spirv.ptr<vector<3xi32>, Input>
  spirv.func @main() -> () "None" {
    %0 = spirv.mlir.addressof @gid : !spirv.ptr<vector<3xi32>, Input>
    %1 = spirv.Load "Input" %0 : vector<3xi32>
    %2 = spirv.CompositeExtract %1[0 : i32] : vector<3xi32>
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main, @gid
  spirv.ExecutionMode @main "LocalSize", 8, 8, 1
}

// CHECK: target triple = "spirv-unknown-vulkan-compute"
// CHECK-NOT: @gid
// CHECK: define void @main() #[[ATTRS:[0-9]+]]
// CHECK: call i32 @llvm.spv.thread.id.i32(i32 0)
// CHECK: call i32 @llvm.spv.thread.id.i32(i32 1)
// CHECK: call i32 @llvm.spv.thread.id.i32(i32 2)
// CHECK: attributes #[[ATTRS]] = { "feme.shader.stage"="compute" "hlsl.numthreads"="8,8,1" "hlsl.shader"="compute" }
