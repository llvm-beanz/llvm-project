// RUN: feme-translate --no-implicit-module --serialize-spirv %s -o %t.spv
// RUN: feme-translate --import-spirv %t.spv | \
// RUN:   feme-translate --no-implicit-module --spirv-to-llvmir - -o %t.ll
// RUN: feme-translate --llvm-backend --target-triple=spirv64-unknown-unknown \
// RUN:   %t.ll -o %t.roundtrip.spv
// RUN: feme-translate --import-spirv %t.roundtrip.spv | FileCheck %s
//
// REQUIRES: spirv-registered-target

// The same "null pipeline" round-trip as spirv-backend-null-pipeline.mlir,
// but for an entry point declaring `VK_KHR_shader_float_controls`'s
// `RoundingModeRTZ` execution mode (roadmap F15a): rather than round-
// tripping back to an unattributed `spirv.FAdd`, the constrained
// `llvm.experimental.constrained.fadd` intrinsic
// feme::spirv::ConvertSPIRVToLLVMPass (SPIRVToLLVMPatterns.cpp's
// ConstrainedRoundTowardZeroPattern) produced for it round-trips through
// LLVM's real, in-tree SPIRV backend into an `spirv.FAdd` with an explicit
// `fp_rounding_mode = #spirv.fp_rounding_mode<RTZ>` attribute -- concrete
// evidence this pipeline actually produces truncating-rounding-mode code,
// not merely a diagnostic that it does not.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, RoundingModeRTZ], []> {
  spirv.func @rounding_mode_rtz(%a: f32, %b: f32) -> (f32) "None" {
    %0 = spirv.FAdd %a, %b : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "Fragment" @rounding_mode_rtz
  spirv.ExecutionMode @rounding_mode_rtz "OriginUpperLeft"
  spirv.ExecutionMode @rounding_mode_rtz "RoundingModeRTZ", 32
}

// CHECK: spirv.func @rounding_mode_rtz
// CHECK: spirv.FAdd %{{.*}}, %{{.*}} {fp_rounding_mode = #spirv.fp_rounding_mode<RTZ>} : f32
