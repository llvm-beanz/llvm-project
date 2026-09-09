// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that a `non_uniform` unit attribute (roadmap L7f -- the MLIR
// SPIR-V dialect's own `NonUniform` decoration, e.g. as dxc emits on the
// `OpCopyObject` result produced by a real `NonUniformResourceIndex()`
// call) survives `feme`'s own SPIR-V-to-LLVM conversion inertly: it is
// simply preserved as an unrecognized discardable attribute on the
// resulting `llvm.add`, rather than being rejected or causing a
// conversion failure. `feme`'s own divergence handling
// (`WaveUniformity.cpp`) performs its own independent analysis rather
// than trusting this SPIR-V-level hint, so no `feme`-side legalization
// pattern needs to interpret this attribute for correctness -- it is
// harmless dead metadata from this pipeline's own point of view.

// CHECK-LABEL: llvm.func @non_uniform_decoration
// CHECK: llvm.add %{{.*}}, %{{.*}} {non_uniform} : i32
// CHECK-NEXT: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.5, [Shader, ShaderNonUniform], []> {
  spirv.func @non_uniform_decoration(%arg: i32) -> i32 "None" {
    %0 = spirv.IAdd %arg, %arg {non_uniform} : i32
    spirv.ReturnValue %0 : i32
  }
  spirv.EntryPoint "GLCompute" @non_uniform_decoration
  spirv.ExecutionMode @non_uniform_decoration "LocalSize", 1, 1, 1
}
