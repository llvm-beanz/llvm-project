// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap L287: `spirv.GL.Cosh` lowers via the GLSL.std.450 spec's own
// literal `0.5 * (exp(x) + exp(-x))` formula (using `llvm.exp`) rather
// than a direct `llvm.cosh` mapping -- see `HyperbolicViaExpPattern`'s
// own comment in SPIRVToLLVMPatterns.cpp for why: CTS's own precision
// tests expect premature overflow-to-infinity at this formula's own
// overflow threshold (dEQP-VK.glsl.builtin.precision.cosh.highp.*),
// which a numerically careful `llvm.cosh`-based libm implementation
// does not produce. Unlike `Sinh` (see
// spirv-to-llvm-transcendental-flush-to-zero.mlir), `Cosh` needs no
// subnormal-input flush -- its own range never comes near zero/subnormal
// scale the way `sinh(x) ~= x` does for small `x`.
// CHECK-LABEL: llvm.func @cosh
// CHECK-NOT: llvm.intr.is.fpclass
// CHECK: %[[NEGX:.*]] = llvm.fneg %arg0 : f32
// CHECK: %[[EXPX:.*]] = llvm.intr.exp(%arg0) : (f32) -> f32
// CHECK: %[[EXPNEGX:.*]] = llvm.intr.exp(%[[NEGX]]) : (f32) -> f32
// CHECK: %[[SUM:.*]] = llvm.fadd %[[EXPX]], %[[EXPNEGX]] : f32
// CHECK: %[[HALF:.*]] = llvm.mlir.constant(5.000000e-01 : f32) : f32
// CHECK: %[[RES:.*]] = llvm.fmul %[[HALF]], %[[SUM]] : f32
// CHECK: llvm.return %[[RES]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @cosh(%a: f32) -> (f32) "None" {
    %0 = spirv.GL.Cosh %a : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "GLCompute" @cosh
  spirv.ExecutionMode @cosh "LocalSize", 1, 1, 1
}
