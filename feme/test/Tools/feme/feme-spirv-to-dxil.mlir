// REQUIRES: spirv-registered-target, directx-registered-target
// RUN: feme-translate --no-implicit-module --serialize-spirv %s -o %t.spv
// RUN: feme --target=dxil %t.spv -o %t.dxil
// RUN: od -An -tx1 -N4 %t.dxil | FileCheck %s

// Retargets a SPIR-V module all the way to DXIL through the full `feme`
// CLI (Design.md milestone 6's "SPIR-V -> DXIL direction", roadmap step
// R13): import (feme::SPIRVImporter) -> translate to llvm::Module
// (feme::SPIRVToLLVMTranslator) -> raise the `llvm.spv.*`/
// `target("spirv.")` conventions back to `llvm.dx.*`/`target("dx.")`
// (feme::dxil::SPIRVRaisingPass) -> feme::DXILExporter. The `feme` CLI
// counterpart to test/Transforms/DXIL/dxil-raise-spirv.ll: the same shape
// of shader (reads the dispatch thread id, reads and writes a bound
// `RWStructuredBuffer<float>` through it) exercised end to end.
//
// Only a `StorageBuffer` block resource reaches this point today: MLIR's
// `SPIRVToLLVM` conversion has no patterns yet for image *access* ops (see
// the "Known gap" note in feme/docs/Design.md's SPIR-V section), so no
// SPIR-V shader reading/writing a typed-buffer image resource reaches LLVM
// IR at all, and this pass's typed-buffer handle raising accordingly has
// nothing to exercise end to end yet either.
//
// Checking the output's first four bytes are the "DXBC" magic (`DXContainer`
// -- the format predates the DXIL name, see feme/docs/Design.md's DXIL
// section) confirms a real DXIL container was produced.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @gid built_in("GlobalInvocationId") : !spirv.ptr<vector<3xi32>, Input>
  spirv.GlobalVariable @out bind(0, 1) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0])>, StorageBuffer>
  spirv.func @main() -> () "None" {
    %0 = spirv.mlir.addressof @gid : !spirv.ptr<vector<3xi32>, Input>
    %1 = spirv.Load "Input" %0 : vector<3xi32>
    %idx = spirv.CompositeExtract %1[0 : i32] : vector<3xi32>
    %2 = spirv.mlir.addressof @out : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0])>, StorageBuffer>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %2[%c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0])>, StorageBuffer>, i32, i32 -> !spirv.ptr<f32, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : f32
    %one = spirv.Constant 1.0 : f32
    %v2 = spirv.FAdd %v, %one : f32
    spirv.Store "StorageBuffer" %ac, %v2 : f32
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main, @gid, @out
  spirv.ExecutionMode @main "LocalSize", 8, 1, 1
}

// CHECK: 44 58 42 43
