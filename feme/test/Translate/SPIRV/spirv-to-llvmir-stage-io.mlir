// RUN: feme-translate --no-implicit-module --spirv-to-llvmir %s | FileCheck %s

// Runs the whole `spirv` dialect -> LLVM IR translation on a fragment shader
// that reads a non-builtin `Input` variable and writes a non-builtin
// `Output` variable, and checks that the result is IR LLVM's SPIRV backend
// understands: an ordinary global in the matching address space (7/8), with
// the `Location`/interpolation decorations preserved as `!spirv.Decorations`
// metadata (see `llvm/lib/Target/SPIRV/SPIRVUtils.cpp`'s
// `buildOpSpirvDecorations` and
// `llvm/test/CodeGen/SPIRV/linkage/hidden-interface-vars.ll`) instead of
// failing to legalize (roadmap R19). See the "SPIR-V -> MLIR `llvm` dialect
// -> LLVM IR" section of feme/docs/Design.md.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @in_var {location = 0 : i32, flat} : !spirv.ptr<i32, Input>
  spirv.GlobalVariable @out_var {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @main() -> () "None" {
    %0 = spirv.mlir.addressof @in_var : !spirv.ptr<i32, Input>
    %1 = spirv.Load "Input" %0 : i32
    %2 = spirv.mlir.addressof @out_var : !spirv.ptr<vector<4xf32>, Output>
    %f = spirv.ConvertSToF %1 : i32 to f32
    %cst = spirv.CompositeConstruct %f, %f, %f, %f : (f32, f32, f32, f32) -> vector<4xf32>
    spirv.Store "Output" %2, %cst : vector<4xf32>
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @main
  spirv.ExecutionMode @main "OriginUpperLeft"
}

// CHECK: target triple = "spirv-unknown-vulkan-pixel"
// CHECK-DAG: @in_var = external addrspace(7) constant i32, !spirv.Decorations ![[IN_DECOS:[0-9]+]]
// CHECK-DAG: @out_var = external addrspace(8) global <4 x float>, !spirv.Decorations ![[OUT_DECOS:[0-9]+]]
// CHECK: define void @main()
// CHECK: load i32, ptr addrspace(7) @in_var
// CHECK: store <4 x float> {{.*}}, ptr addrspace(8) @out_var
// CHECK-DAG: ![[IN_DECOS]] = !{![[LOC0:[0-9]+]], ![[FLAT:[0-9]+]]}
// CHECK-DAG: ![[LOC0]] = !{i32 30, i32 0}
// CHECK-DAG: ![[FLAT]] = !{i32 14}
// CHECK-DAG: ![[OUT_DECOS]] = !{![[LOC0]]}
