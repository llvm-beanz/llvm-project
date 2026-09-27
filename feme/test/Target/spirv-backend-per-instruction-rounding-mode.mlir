// RUN: feme-translate --no-implicit-module --serialize-spirv %s -o %t.spv
// RUN: feme-translate --import-spirv %t.spv | \
// RUN:   feme-translate --no-implicit-module --spirv-to-llvmir - | FileCheck %s
//
// REQUIRES: spirv-registered-target

// The same round-trip as spirv-backend-rounding-mode-rtz.mlir (roadmap
// F15a), but for a per-instruction `FPRoundingMode` decoration
// (`VK_KHR_shader_float_controls2`, roadmap F15c) rather than a
// whole-entry-point `RoundingModeRTZ` execution mode, and for the
// round-toward-positive direction F15a's own whole-entry-point mode has no
// way to even express.
//
// (Roadmap L214) As in spirv-backend-rounding-mode-rtz.mlir, this test no
// longer round-trips a second time through LLVM's real SPIRV backend --
// see that test's own comment for why (`llvm.set.rounding`, which L214's
// fix now always brackets a non-default-rounding-mode constrained op
// with, cannot be legalized by the SPIRV backend at all). Checks the
// conversion's own LLVM IR output directly instead.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @rounding_mode_rtp(%a: f32, %b: f32) -> (f32) "None" {
    %0 = spirv.FAdd %a, %b {fp_rounding_mode = #spirv.fp_rounding_mode<RTP>} : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "Fragment" @rounding_mode_rtp
  spirv.ExecutionMode @rounding_mode_rtp "OriginUpperLeft"
}

// CHECK-LABEL: define float @rounding_mode_rtp
// CHECK: %[[OLD:.*]] = call i32 @llvm.get.rounding()
// CHECK: call void @llvm.set.rounding(i32 2)
// CHECK: call float @llvm.experimental.constrained.fadd.f32(float %{{.*}}, float %{{.*}}, metadata !"round.dynamic", metadata !"fpexcept.ignore")
// CHECK: call void @llvm.set.rounding(i32 %[[OLD]])
