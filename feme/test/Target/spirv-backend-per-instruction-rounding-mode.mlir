// RUN: feme-translate --no-implicit-module --serialize-spirv %s -o %t.spv
// RUN: feme-translate --import-spirv %t.spv | \
// RUN:   feme-translate --no-implicit-module --spirv-to-llvmir - -o %t.ll
// RUN: feme-translate --llvm-backend --target-triple=spirv64-unknown-unknown \
// RUN:   %t.ll -o %t.roundtrip.spv
// RUN: feme-translate --import-spirv %t.roundtrip.spv | FileCheck %s
//
// REQUIRES: spirv-registered-target

// The same "null pipeline" round-trip as spirv-backend-rounding-mode-rtz.mlir
// (roadmap F15a), but for a per-instruction `FPRoundingMode` decoration
// (`VK_KHR_shader_float_controls2`, roadmap F15c) rather than a
// whole-entry-point `RoundingModeRTZ` execution mode, and for the
// round-toward-positive direction F15a's own whole-entry-point mode has no
// way to even express: the constrained `llvm.experimental.constrained.fadd`
// `FloatControlArithmeticPattern` (SPIRVToLLVMPatterns.cpp) produces for it
// round-trips through LLVM's real, in-tree SPIRV backend back into an
// `spirv.FAdd` with an explicit `fp_rounding_mode = #spirv.fp_rounding_mode<RTP>`
// attribute -- concrete evidence this pipeline actually produces
// round-toward-positive code for a per-instruction decoration, not merely a
// FileCheck pattern over the conversion's own IR.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @rounding_mode_rtp(%a: f32, %b: f32) -> (f32) "None" {
    %0 = spirv.FAdd %a, %b {fp_rounding_mode = #spirv.fp_rounding_mode<RTP>} : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "Fragment" @rounding_mode_rtp
  spirv.ExecutionMode @rounding_mode_rtp "OriginUpperLeft"
}

// CHECK: spirv.func @rounding_mode_rtp
// CHECK: spirv.FAdd %{{.*}}, %{{.*}} {fp_rounding_mode = #spirv.fp_rounding_mode<RTP>} : f32
