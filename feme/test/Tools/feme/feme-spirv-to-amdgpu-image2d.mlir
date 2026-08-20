// REQUIRES: spirv-registered-target, amdgpu-registered-target
// RUN: feme-translate --no-implicit-module --serialize-spirv %s -o %t.spv
// RUN: feme --target=amdgcn-amd-amdhsa %t.spv -o %t.o
// RUN: od -An -tx1 -N4 %t.o | FileCheck %s

// The `feme` CLI counterpart to test/Tools/feme/feme-spirv-to-amdgpu.mlir,
// but reading a genuinely 2D `Texture2D` (via `spirv.ImageFetch` with a lone
// `Lod` operand -- what `dxc` always emits for `Texture2D<T>::Load`, even a
// literal 0 mip -- see `ImageFetchLodPattern` in SPIRVToLLVMPatterns.cpp)
// and writing a 2D `RWTexture2D` (via `spirv.ImageWrite`) instead of that
// test's 1D `Buffer` pair, exercising feme::amdgpu::ResourceLoweringPass's
// multi-dimensional `spirv.Image` coordinate linearization end to end (see
// the "Raised LLVM IR -> AMDGPU" section of feme/docs/Design.md) -- this is
// the exact shape a real `dxc -T cs_6_x` compute shader reading and writing
// a `Texture2D`/`RWTexture2D` pair produces. Checking the output's first
// four bytes are the ELF magic number confirms a real object file was
// produced, without needing `llvm-objdump` in this build.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @gid built_in("GlobalInvocationId") : !spirv.ptr<vector<3xi32>, Input>
  spirv.GlobalVariable @in bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @out bind(0, 1) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
  spirv.func @main() -> () "None" {
    %0 = spirv.mlir.addressof @gid : !spirv.ptr<vector<3xi32>, Input>
    %1 = spirv.Load "Input" %0 : vector<3xi32>
    %x = spirv.CompositeExtract %1[0 : i32] : vector<3xi32>
    %y = spirv.CompositeExtract %1[1 : i32] : vector<3xi32>
    %coord = spirv.CompositeConstruct %x, %y : (i32, i32) -> vector<2xi32>
    %lod = spirv.Constant 0 : si32
    %2 = spirv.mlir.addressof @in : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %texel = spirv.ImageFetch %3, %coord ["Lod"], %lod : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, vector<2xi32>, si32 -> vector<4xf32>
    %4 = spirv.mlir.addressof @out : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
    %5 = spirv.Load "UniformConstant" %4 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>
    "spirv.ImageWrite"(%5, %coord, %texel) <{image_operands = #spirv.image_operands<None>}> : (!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, vector<2xi32>, vector<4xf32>) -> ()
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main, @gid, @in, @out
  spirv.ExecutionMode @main "LocalSize", 8, 8, 1
}

// CHECK: 7f 45 4c 46
