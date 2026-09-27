// RUN: feme-translate --no-implicit-module --serialize-spirv %s -o %t.spv
// RUN: feme-translate --import-spirv %t.spv | \
// RUN:   feme-translate --no-implicit-module --spirv-to-llvmir - | FileCheck %s
//
// REQUIRES: spirv-registered-target

// An entry point declaring `VK_KHR_shader_float_controls`'s
// `RoundingModeRTZ` execution mode (roadmap F15a): the arithmetic op
// becomes a constrained intrinsic rather than round-tripping back to an
// unattributed `spirv.FAdd`.
//
// (Roadmap L214) This test previously round-tripped the resulting LLVM IR
// a second time, back through LLVM's real, in-tree SPIRV backend
// (`--llvm-backend --target-triple=spirv64-unknown-unknown`), as
// "concrete evidence" of correct codegen. That second round-trip is no
// longer possible: the fix for L214 (a static, non-default rounding mode
// silently discarded by real hardware backends, e.g. AArch64, with zero
// `FPCR` manipulation -- confirmed via an isolated `.ll` reproducer)
// brackets the constrained op with real `llvm.get.rounding`/
// `llvm.set.rounding` calls and gives the op itself `dynamic`, not a
// static, rounding-mode metadata -- and LLVM's SPIRV backend cannot
// legalize `llvm.set.rounding` at all (`LLVM ERROR: unable to legalize
// instruction: G_SET_ROUNDING`, confirmed by hand while fixing L214,
// since SPIR-V's own `FPRoundingMode` decoration is a static,
// compile-time-only hint with no runtime-FPU-state equivalent to lower a
// *dynamic* rounding-mode request to). This test therefore now stops at
// the conversion's own LLVM IR output and checks that IR directly, the
// same level spirv-to-llvm-rounding-mode-rtz.mlir
// (test/Conversion/SPIRVToLLVM) already checks at the MLIR level; real,
// assembly-level backend codegen correctness for this fix was separately
// confirmed with hand-written `.ll` reproducers compiled via
// `llc -mtriple=aarch64-linux-gnu` (see agent_thoughts.md's own L214
// entry), not by this lit test.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, RoundingModeRTZ], []> {
  spirv.func @rounding_mode_rtz(%a: f32, %b: f32) -> (f32) "None" {
    %0 = spirv.FAdd %a, %b : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "Fragment" @rounding_mode_rtz
  spirv.ExecutionMode @rounding_mode_rtz "OriginUpperLeft"
  spirv.ExecutionMode @rounding_mode_rtz "RoundingModeRTZ", 32
}

// CHECK-LABEL: define float @rounding_mode_rtz
// CHECK: %[[OLD:.*]] = call i32 @llvm.get.rounding()
// CHECK: call void @llvm.set.rounding(i32 0)
// CHECK: call float @llvm.experimental.constrained.fadd.f32(float %{{.*}}, float %{{.*}}, metadata !"round.dynamic", metadata !"fpexcept.ignore")
// CHECK: call void @llvm.set.rounding(i32 %[[OLD]])
