// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that a `spirv.module`'s entry points survive the conversion as the
// `hlsl.shader`/`hlsl.numthreads` function attributes LLVM's SPIRV backend
// reads them from (`llvm/lib/Target/SPIRV/SPIRVCallLowering.cpp`,
// `SPIRVAsmPrinter.cpp`), rather than as MLIR's `__spv__*_execution_mode_info_*`
// globals, which describe the same thing to MLIR's SPIR-V *runner* and mean
// nothing to the backend, and that each also carries FeMe's own
// source-independent `feme.shader.stage` enumeration ("Stage identity" in
// feme/docs/FeMeGraphicsDesign.md).

// CHECK-NOT: __spv__
// CHECK: llvm.func @compute_entry()
// CHECK-SAME: passthrough = {{\[}}["hlsl.shader", "compute"], ["feme.shader.stage", "compute"], ["hlsl.numthreads", "8,4,1"]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @compute_entry() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @compute_entry
  spirv.ExecutionMode @compute_entry "LocalSize", 8, 4, 1
}

// -----

// A stage with no workgroup dimensions gets no `hlsl.numthreads`. Note the
// two spellings of the one stage: `hlsl.shader` keeps Direct3D's `pixel`,
// which is what the SPIRV backend and the module triple use, while
// `feme.shader.stage` uses FeMe's own `fragment`.

// CHECK: llvm.func @pixel_entry()
// CHECK-SAME: passthrough = {{\[}}["hlsl.shader", "pixel"], ["feme.shader.stage", "fragment"]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @pixel_entry() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @pixel_entry
  spirv.ExecutionMode @pixel_entry "OriginUpperLeft"
}

// -----

// A function that is not an entry point is left alone.

// CHECK: llvm.func @helper()
// CHECK-NOT: hlsl.shader
// CHECK-NOT: feme.shader.stage
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @helper() -> () "None" {
    spirv.Return
  }
}
