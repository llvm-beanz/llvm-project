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
// feme/docs/Design.md). Checking the output's first four bytes are the ELF
// magic number confirms a real object file was produced, without needing
// `llvm-objdump` in this build.

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

// CHECK: 7f 45 4c 46
