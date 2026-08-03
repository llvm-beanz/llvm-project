// REQUIRES: spirv-registered-target, amdgpu-registered-target
// RUN: feme-translate --no-implicit-module --serialize-spirv %s -o %t.spv
// RUN: feme --from=spirv --target=amdgcn-amd-amdhsa %t.spv -o %t.o
// RUN: od -An -tx1 -N4 %t.o | FileCheck %s

// Retargets a SPIR-V module all the way to a real ISA (AMDGPU) through the
// full `feme` CLI: import (feme::SPIRVImporter) -> translate to
// llvm::Module (feme::SPIRVToLLVMTranslator) -> lower the raised
// `llvm.spv.*` ops to AMDGPU's own (feme::amdgpu::RaisedLoweringPass,
// feme::amdgpu::ResourceLoweringPass) -> feme::TargetMachineBackend
// targeting "amdgcn-amd-amdhsa". The `feme` CLI counterpart to
// test/Tools/feme/feme-dxil-to-amdgpu.ll: the same shape of shader (reads a
// builtin dispatch index, reads and writes a bound resource through it) as
// that test's DXIL input, here starting from SPIR-V instead, exercising the
// `llvm.spv.*` half of feme::amdgpu::{Raised,Resource}LoweringPass's
// coverage (see the "Raised LLVM IR -> AMDGPU" section of
// feme/docs/Design.md). Also exercises a dynamically-indexed local array
// constant and a `CompositeConstruct`-built vector -- the shapes a real
// `dxc`-compiled shader with a `const static` palette array produces (see
// feme::spirv::ArrayConstantPattern/CompositeConstructPattern in
// SPIRVToLLVMPatterns.cpp) -- and an explicit `#spirv.image_operands<None>`
// attribute on the image access, which real `dxc` output always carries
// (see feme::spirv::hasImageOperands). Checking the output's first four
// bytes are the ELF magic number confirms a real object file was produced,
// without needing `llvm-objdump` in this build.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, ImageBuffer], []> {
  spirv.GlobalVariable @gid built_in("GlobalInvocationId") : !spirv.ptr<vector<3xi32>, Input>
  spirv.GlobalVariable @buf bind(0, 1) : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
  spirv.func @main() -> () "None" {
    %pal = spirv.Variable : !spirv.ptr<!spirv.array<2 x vector<3xf32>>, Function>
    %palette = spirv.Constant [dense<0.0> : vector<3xf32>, dense<1.0> : vector<3xf32>] : !spirv.array<2 x vector<3xf32>>
    spirv.Store "Function" %pal, %palette : !spirv.array<2 x vector<3xf32>>
    %0 = spirv.mlir.addressof @gid : !spirv.ptr<vector<3xi32>, Input>
    %1 = spirv.Load "Input" %0 : vector<3xi32>
    %2 = spirv.CompositeExtract %1[0 : i32] : vector<3xi32>
    %c1 = spirv.Constant 1 : i32
    %idx = spirv.BitwiseAnd %2, %c1 : i32
    %palptr = spirv.AccessChain %pal[%idx] : !spirv.ptr<!spirv.array<2 x vector<3xf32>>, Function>, i32 -> !spirv.ptr<vector<3xf32>, Function>
    %color = spirv.Load "Function" %palptr : vector<3xf32>
    %r = spirv.CompositeExtract %color[0 : i32] : vector<3xf32>
    %g = spirv.CompositeExtract %color[1 : i32] : vector<3xf32>
    %b = spirv.CompositeExtract %color[2 : i32] : vector<3xf32>
    %one = spirv.Constant 1.0 : f32
    %texel = spirv.CompositeConstruct %r, %g, %b, %one : (f32, f32, f32, f32) -> vector<4xf32>
    %3 = spirv.mlir.addressof @buf : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
    %4 = spirv.Load "UniformConstant" %3 : !spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>
    "spirv.ImageWrite"(%4, %2, %texel) <{image_operands = #spirv.image_operands<None>}> : (!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, i32, vector<4xf32>) -> ()
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main, @gid, @buf
  spirv.ExecutionMode @main "LocalSize", 8, 8, 1
}

// CHECK: 7f 45 4c 46
