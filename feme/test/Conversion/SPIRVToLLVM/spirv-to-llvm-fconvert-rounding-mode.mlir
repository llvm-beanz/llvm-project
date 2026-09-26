// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// (Roadmap L208) A per-instruction `FPRoundingMode RTZ` decoration
// (`VK_KHR_shader_float_controls2`, roadmap F15c) on a narrowing
// `spirv.FConvert` is now honored by `FConvertRoundingModePattern`
// (SPIRVToLLVMPatterns.cpp), rather than silently falling through to
// upstream's own `IndirectCastPattern<spirv::FConvertOp, ...>`, which always
// produces a plain `llvm.fptrunc` (round-to-nearest-even, ignoring the
// decoration entirely). Unlike `FloatControlArithmeticPattern`'s own five
// binary arithmetic ops (which lower a non-default rounding mode through
// `llvm.experimental.constrained.*`), this pattern deliberately does *not*
// use a constrained intrinsic at all: an isolated pair of standalone `.ll`
// reproducers (see `agent_thoughts.md`'s own L208 entry) confirmed AArch64's
// backend silently discards a constrained op's own explicit non-default
// rounding-mode metadata at codegen time, with no `FPCR` manipulation
// whatsoever -- a genuine LLVM/AArch64 backend correctness gap, out of
// scope to fix here. Round-toward-zero is instead built entirely out of
// ordinary integer bit manipulation (extract sign/exponent/mantissa,
// truncate the mantissa to the narrower format's width, reassemble), which
// this test only spot-checks the characteristic shape of (the full
// expansion is long and not the point of this test) rather than matching
// every single generated instruction.

// CHECK-NOT: __spv__
// CHECK-LABEL: llvm.func @fconvert_rtz
// CHECK-NOT: strictfp
// CHECK: llvm.bitcast %{{.*}} : f32 to i32
// CHECK: llvm.trunc %{{.*}} : i32 to i16
// CHECK: llvm.bitcast %{{.*}} : i16 to f16
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @fconvert_rtz(%a: f32) -> (f16) "None" {
    %0 = spirv.FConvert %a {fp_rounding_mode = #spirv.fp_rounding_mode<RTZ>} : f32 to f16
    spirv.ReturnValue %0 : f16
  }
  spirv.EntryPoint "Fragment" @fconvert_rtz
  spirv.ExecutionMode @fconvert_rtz "OriginUpperLeft"
}

// -----

// A vector-typed narrowing `FConvert` decomposes into one scalar
// bit-manipulation conversion per lane (mirroring `GLLdexpPattern`'s own
// existing per-lane-decomposition precedent), reassembled back into a
// vector result.
// CHECK-LABEL: llvm.func @fconvert_rtz_vector
// CHECK: llvm.extractelement
// CHECK: llvm.trunc %{{.*}} : i32 to i16
// CHECK: llvm.insertelement
// CHECK: llvm.extractelement
// CHECK: llvm.trunc %{{.*}} : i32 to i16
// CHECK: llvm.insertelement
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @fconvert_rtz_vector(%a: vector<2xf32>) -> (vector<2xf16>) "None" {
    %0 = spirv.FConvert %a {fp_rounding_mode = #spirv.fp_rounding_mode<RTZ>} : vector<2xf32> to vector<2xf16>
    spirv.ReturnValue %0 : vector<2xf16>
  }
  spirv.EntryPoint "Fragment" @fconvert_rtz_vector
  spirv.ExecutionMode @fconvert_rtz_vector "OriginUpperLeft"
}

// -----

// An explicit `RTE` decoration (already plain `fptrunc`'s own natural
// rounding behavior) falls through to upstream's `IndirectCastPattern`
// unchanged -- this pattern only ever intervenes for a genuine, non-default
// rounding direction, matching `FloatControlArithmeticPattern`'s own
// identical precedent.
// CHECK-LABEL: llvm.func @fconvert_rte_falls_through
// CHECK: llvm.fptrunc %{{.*}} : f32 to f16
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @fconvert_rte_falls_through(%a: f32) -> (f16) "None" {
    %0 = spirv.FConvert %a {fp_rounding_mode = #spirv.fp_rounding_mode<RTE>} : f32 to f16
    spirv.ReturnValue %0 : f16
  }
  spirv.EntryPoint "Fragment" @fconvert_rte_falls_through
  spirv.ExecutionMode @fconvert_rte_falls_through "OriginUpperLeft"
}

// -----

// An undecorated `FConvert` also falls through unchanged.
// CHECK-LABEL: llvm.func @fconvert_undecorated_falls_through
// CHECK: llvm.fptrunc %{{.*}} : f32 to f16
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @fconvert_undecorated_falls_through(%a: f32) -> (f16) "None" {
    %0 = spirv.FConvert %a : f32 to f16
    spirv.ReturnValue %0 : f16
  }
  spirv.EntryPoint "Fragment" @fconvert_undecorated_falls_through
  spirv.ExecutionMode @fconvert_undecorated_falls_through "OriginUpperLeft"
}

// -----

// A *widening* conversion is exact regardless of any rounding-mode
// decoration, so this pattern never fires for it even when `RTZ` is
// (meaninglessly) present.
// CHECK-LABEL: llvm.func @fconvert_widening_ignores_decoration
// CHECK: llvm.fpext %{{.*}} : f16 to f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Float16], []> {
  spirv.func @fconvert_widening_ignores_decoration(%a: f16) -> (f32) "None" {
    %0 = spirv.FConvert %a {fp_rounding_mode = #spirv.fp_rounding_mode<RTZ>} : f16 to f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "Fragment" @fconvert_widening_ignores_decoration
  spirv.ExecutionMode @fconvert_widening_ignores_decoration "OriginUpperLeft"
}
