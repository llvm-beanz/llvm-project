// REQUIRES: spirv-registered-target, amdgpu-registered-target
// RUN: feme-translate --no-implicit-module --serialize-spirv %s -o %t.spv
// RUN: feme --from=spirv --target=amdgcn-amd-amdhsa %t.spv -o %t.o
// RUN: od -An -tx1 -N4 %t.o | FileCheck %s

// Retargets a SPIR-V module all the way to a real ISA (AMDGPU) through the
// full `feme` CLI: import (feme::SPIRVImporter) -> translate to
// llvm::Module (feme::SPIRVToLLVMTranslator) -> feme::TargetMachineBackend
// targeting "amdgcn-amd-amdhsa". Uses a function with no SPIR-V builtins/
// resource ops (a real shader's, since feme::amdgpu::RaisedLoweringPass does
// not yet re-express SPIR-V builtin-variable equivalents for AMDGPU -- see
// the "Raised LLVM IR -> AMDGPU" section of feme/docs/Design.md), so this
// exercises the Driver/Translator/Backend plumbing without depending on
// coverage that doesn't exist yet. Checking the output's first four bytes
// are the ELF magic number confirms a real object file was produced,
// without needing `llvm-objdump` in this build.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @foo() -> () "Inline" {
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @foo
}

// CHECK: 7f 45 4c 46
