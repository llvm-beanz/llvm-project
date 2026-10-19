// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// `VK_KHR_shader_float_controls2`'s per-instruction decorations (roadmap
// F15c) are now honored, reusing F15a/F15b's `FloatControlArithmeticPattern`
// (SPIRVToLLVMPatterns.cpp): `FPRoundingMode` is read directly off the
// individual `spirv.FAdd`/etc. op it decorates (MLIR's own deserializer
// already attaches it there, unlike the whole-entry-point execution modes
// F15a/F15b handle), rather than only from a whole-entry-point execution
// mode, and can independently request any of the four IEEE rounding
// directions -- not just round-toward-zero. `FPFastMathMode` is a separate,
// additive mechanism (ordinary LLVM fast-math flags), not another
// constrained-intrinsics consumer.

// (Roadmap L214) Every non-default rounding-mode direction below (`RTZ`/
// `RTP`/`RTN`) is checked as a `llvm.get.rounding`/`llvm.set.rounding`
// pair bracketing a `dynamic`-mode constrained intrinsic, not a *static*
// rounding-mode attribute directly on the intrinsic: an isolated `.ll`
// reproducer confirmed this host's AArch64 backend silently drops a
// static non-default rounding mode with zero `FPCR` manipulation at all,
// the same class of bug roadmap L208 found and fixed for `FConvert`
// specifically. See spirv-to-llvm-rounding-mode-rtz.mlir's own comment
// for the full explanation.
//
// (Roadmap L456) Every f32 case below also now flushes operands/results
// to a same-signed zero around each arithmetic op, with no `DenormFlushToZero`/
// `DenormPreserve` execution mode declared at all: see
// spirv-to-llvm-rounding-mode-rtz.mlir's own L456 comment for why f32
// alone flushes by default now, unless `DenormPreserve` is declared.

// A per-instruction `FPRoundingMode` decoration applies with no
// whole-entry-point execution mode declared at all.
// CHECK-LABEL: llvm.func @rtz_decoration
// CHECK-SAME: attributes {passthrough = [{{.*}}"strictfp"]}
// CHECK: "llvm.intr.is.fpclass"(%arg0)
// CHECK: %[[A:.*]] = llvm.select {{.*}} : i1, f32
// CHECK: "llvm.intr.is.fpclass"(%arg1)
// CHECK: %[[B:.*]] = llvm.select {{.*}} : i1, f32
// CHECK: %[[OLD:.*]] = llvm.call_intrinsic "llvm.get.rounding"()
// CHECK: %[[RTZ:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[RTZ]])
// CHECK: %[[SUM:.*]] = llvm.intr.experimental.constrained.fadd %[[A]], %[[B]] dynamic ignore : f32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[OLD]])
// CHECK: "llvm.intr.is.fpclass"(%[[SUM]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @rtz_decoration(%a: f32, %b: f32) -> (f32) "None" {
    %0 = spirv.FAdd %a, %b {fp_rounding_mode = #spirv.fp_rounding_mode<RTZ>} : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "Fragment" @rtz_decoration
  spirv.ExecutionMode @rtz_decoration "OriginUpperLeft"
}

// -----

// `RTP`/`RTN`, which the whole-entry-point `RoundingModeRTZ` execution mode
// (roadmap F15a) has no way to express at all, round-trip through the same
// constrained-intrinsics lowering.
// CHECK-LABEL: llvm.func @rtp_rtn_decorations
// CHECK: %[[A:.*]] = llvm.select {{.*}} : i1, f32
// CHECK: %[[B:.*]] = llvm.select {{.*}} : i1, f32
// CHECK: %[[OLD0:.*]] = llvm.call_intrinsic "llvm.get.rounding"()
// CHECK: %[[RTP:.*]] = llvm.mlir.constant(2 : i32) : i32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[RTP]])
// CHECK: %[[SUM:.*]] = llvm.intr.experimental.constrained.fadd %[[A]], %[[B]] dynamic ignore : f32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[OLD0]])
// CHECK: %[[FLUSHED_SUM:.*]] = llvm.select {{.*}}, %[[SUM]] : i1, f32
// CHECK: %[[C:.*]] = llvm.select {{.*}}, %[[FLUSHED_SUM]] : i1, f32
// CHECK: %[[D:.*]] = llvm.select {{.*}}, %arg1 : i1, f32
// CHECK: %[[OLD1:.*]] = llvm.call_intrinsic "llvm.get.rounding"()
// CHECK: %[[RTN:.*]] = llvm.mlir.constant(3 : i32) : i32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[RTN]])
// CHECK: llvm.intr.experimental.constrained.fsub %[[C]], %[[D]] dynamic ignore : f32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[OLD1]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @rtp_rtn_decorations(%a: f32, %b: f32) -> (f32) "None" {
    %0 = spirv.FAdd %a, %b {fp_rounding_mode = #spirv.fp_rounding_mode<RTP>} : f32
    %1 = spirv.FSub %0, %b {fp_rounding_mode = #spirv.fp_rounding_mode<RTN>} : f32
    spirv.ReturnValue %1 : f32
  }
  spirv.EntryPoint "Fragment" @rtp_rtn_decorations
  spirv.ExecutionMode @rtp_rtn_decorations "OriginUpperLeft"
}

// -----

// An explicit per-instruction `RTE` decoration overrides the entry point's
// own `RoundingModeRTZ` back to plain, round-to-nearest-even code for that
// one instruction; the sibling `spirv.FSub` (undecorated) still honors the
// entry point's `RoundingModeRTZ`. The decoration itself does not leak
// through onto the resulting `llvm.fadd` as a stale attribute: unlike
// MLIR's own lower-benefit `DirectConversionPattern` (which forwards every
// attribute an op carries verbatim), this pattern always consumes an
// `FPRoundingMode` decoration it matches on, whether or not it changes the
// generated code.
// CHECK-LABEL: llvm.func @rte_override
// CHECK-SAME: attributes {passthrough = [{{.*}}"strictfp"]}
// CHECK: %[[A:.*]] = llvm.select {{.*}} : i1, f32
// CHECK: %[[B:.*]] = llvm.select {{.*}} : i1, f32
// CHECK: %[[SUM:.*]] = llvm.fadd %[[A]], %[[B]] : f32
// CHECK-NOT: fp_rounding_mode
// CHECK: %[[FLUSHED_SUM:.*]] = llvm.select {{.*}}, %[[SUM]] : i1, f32
// CHECK: %[[C:.*]] = llvm.select {{.*}}, %[[FLUSHED_SUM]] : i1, f32
// CHECK: %[[D:.*]] = llvm.select {{.*}}, %arg1 : i1, f32
// CHECK: %[[OLD:.*]] = llvm.call_intrinsic "llvm.get.rounding"()
// CHECK: %[[RTZ:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[RTZ]])
// CHECK: llvm.intr.experimental.constrained.fsub %[[C]], %[[D]] dynamic ignore : f32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[OLD]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, RoundingModeRTZ], []> {
  spirv.func @rte_override(%a: f32, %b: f32) -> (f32) "None" {
    %0 = spirv.FAdd %a, %b {fp_rounding_mode = #spirv.fp_rounding_mode<RTE>} : f32
    %1 = spirv.FSub %0, %b : f32
    spirv.ReturnValue %1 : f32
  }
  spirv.EntryPoint "Fragment" @rte_override
  spirv.ExecutionMode @rte_override "OriginUpperLeft"
  spirv.ExecutionMode @rte_override "RoundingModeRTZ", 32
}

// -----

// `FPFastMathMode` translates to LLVM's ordinary fast-math flags on the
// plain op, an additive mechanism independent of any rounding-mode
// decoration or execution mode. (Roadmap L456) f32 still flushes by
// default around the plain op, same as any other f32 arithmetic with no
// `DenormFlushToZero`/`DenormPreserve` declared.
// CHECK-LABEL: llvm.func @fast_math_decoration
// CHECK-NOT: strictfp
// CHECK: %[[A:.*]] = llvm.select {{.*}} : i1, f32
// CHECK: %[[B:.*]] = llvm.select {{.*}} : i1, f32
// CHECK: llvm.fadd %[[A]], %[[B]] fastmath<nnan, ninf, nsz> : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @fast_math_decoration(%a: f32, %b: f32) -> (f32) "None" {
    %0 = spirv.FAdd %a, %b {fp_fast_math_mode = #spirv.fastmath_mode<NotNaN|NotInf|NSZ>} : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "Fragment" @fast_math_decoration
  spirv.ExecutionMode @fast_math_decoration "OriginUpperLeft"
}

// -----

// SPIR-V's core `Fast` bit maps to LLVM's own `fast` group, which implies
// every individual flag at once.
// CHECK-LABEL: llvm.func @fast_math_fast_bit
// CHECK: %[[A:.*]] = llvm.select {{.*}} : i1, f32
// CHECK: %[[B:.*]] = llvm.select {{.*}} : i1, f32
// CHECK: llvm.fadd %[[A]], %[[B]] fastmath<fast> : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @fast_math_fast_bit(%a: f32, %b: f32) -> (f32) "None" {
    %0 = spirv.FAdd %a, %b {fp_fast_math_mode = #spirv.fastmath_mode<Fast>} : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "Fragment" @fast_math_fast_bit
  spirv.ExecutionMode @fast_math_fast_bit "OriginUpperLeft"
}

// -----

// A non-default rounding-mode decoration and a fast-math decoration on the
// very same instruction: the rounding mode is honored (as a constrained
// intrinsic) and the fast-math flags are dropped, since LLVM's constrained
// intrinsics carry no fast-math flags of their own
// (`LLVM_ConstrainedIntr`, `LLVMIntrinsicOps.td`, `requiresFastmath=0`) --
// a deliberate, narrow scoping decision, not an oversight.
// CHECK-LABEL: llvm.func @rounding_and_fast_math_same_instruction
// CHECK: %[[OLD:.*]] = llvm.call_intrinsic "llvm.get.rounding"()
// CHECK: %[[RTZ:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[RTZ]])
// CHECK: llvm.intr.experimental.constrained.fadd %{{.*}}, %{{.*}} dynamic ignore : f32
// CHECK-NOT: fastmath
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[OLD]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @rounding_and_fast_math_same_instruction(%a: f32, %b: f32) -> (f32) "None" {
    %0 = spirv.FAdd %a, %b {fp_rounding_mode = #spirv.fp_rounding_mode<RTZ>,
                            fp_fast_math_mode = #spirv.fastmath_mode<Fast>} : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "Fragment" @rounding_and_fast_math_same_instruction
  spirv.ExecutionMode @rounding_and_fast_math_same_instruction "OriginUpperLeft"
}

// -----

// `DenormFlushToZero` (F15b, an entry-point-wide execution mode --
// `VK_KHR_shader_float_controls2` adds no per-instruction denorm
// decoration at all) and a per-instruction `FPRoundingMode` decoration (F15c)
// both apply to the same instruction independently: operands/result are
// flushed in software, and the operation itself is the requested
// constrained intrinsic.
// CHECK-LABEL: llvm.func @flush_and_per_instruction_rounding
// CHECK: "llvm.intr.is.fpclass"
// CHECK: %[[OLD:.*]] = llvm.call_intrinsic "llvm.get.rounding"()
// CHECK: %[[RTP:.*]] = llvm.mlir.constant(2 : i32) : i32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[RTP]])
// CHECK: llvm.intr.experimental.constrained.fadd %{{.*}}, %{{.*}} dynamic ignore : f32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[OLD]])
// CHECK: "llvm.intr.is.fpclass"
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, DenormFlushToZero], []> {
  spirv.func @flush_and_per_instruction_rounding(%a: f32, %b: f32) -> (f32) "None" {
    %0 = spirv.FAdd %a, %b {fp_rounding_mode = #spirv.fp_rounding_mode<RTP>} : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "Fragment" @flush_and_per_instruction_rounding
  spirv.ExecutionMode @flush_and_per_instruction_rounding "OriginUpperLeft"
  spirv.ExecutionMode @flush_and_per_instruction_rounding "DenormFlushToZero", 32
}
