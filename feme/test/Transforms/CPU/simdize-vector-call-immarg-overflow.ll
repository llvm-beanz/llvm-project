; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L266 (follow-up): a *genuinely vector-typed* divergent call --
; e.g. `findMSB(ivec4)`'s `llvm.ctlz.v4i32` or `uaddCarry(uvec4,uvec4)`'s
; `llvm.uadd.with.overflow.v4i32` -- goes through
; `checkVectorDecompositionSupported`'s vector-decomposition loop, not the
; scalar-divergent-call path `simdize-divergent-call-ctlz-overflow.ll`
; already covers (`getDivergentCallOverloadShape`, which only ever sees a
; genuinely scalar call result). Two distinct bugs in that loop, both found
; by tracing a real `dEQP-VK.glsl.builtin.function.integer.{findMSB,
; uaddcarry}` failure's pre-SIMDize IR (`FEME_DUMP_IR_PRESIMD=1`), are
; covered here:
;   1. `llvm.ctlz`/`cttz`'s `is_zero_poison` `ImmArg` operand made the
;      loop's plain `all_of(args, arg->getType() == I.getType())`
;      homogeneity check reject the call outright, at *three* separate
;      call sites (vector producer check, vector consumer check, and the
;      `widenInstruction` dispatch gate) -- `callNonImmArgsHaveType` fixes
;      all three.
;   2. an operand feeding one of the four `isOverflowArithIntrinsic` calls
;      (e.g. `uaddCarry`'s two `uvec4` operands) had no consumer-acceptance
;      rule at all, since the call's own *result* is aggregate-typed and
;      validated by `checkAggregateValueSupported` instead of this
;      vector-decomposition loop -- its *operands* still need this loop's
;      own consumer rule, since they are ordinary vector-typed values.
; Neither test in this file exercises the scalar-call path at all: `%v0`'s
; only producer is a genuinely-divergent `insertelement` chain (not a
; `feme.stage.input.load`-style uniform value), so the whole chain is
; widened, not per-lane-cloned.

; CHECK-LABEL: define void @main(
; `is_zero_poison` stays scalar `i1` even though the ctlz's other operand
; widens to `<4 x i32>`.
; CHECK: call <4 x i32> @llvm.ctlz.v4i32(<4 x i32> {{%.*}}, i1 false)
; The with-overflow call's own two `<4 x i32>` operands (fed by `%v0`/
; `%v1`, both genuinely divergent vectors) are accepted and widened, not
; diagnosed as an unsupported consumer.
; CHECK: call { <4 x i32>, <4 x i1> } @llvm.uadd.with.overflow.v4i32(<4 x i32> {{%.*}}, <4 x i32> {{.*}})
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidf = sitofp i32 %tid to float
  %tidi = fptosi float %tidf to i32

  %v00 = insertelement <4 x i32> poison, i32 %tidi, i32 0
  %v01 = insertelement <4 x i32> %v00, i32 1, i32 1
  %v02 = insertelement <4 x i32> %v01, i32 2, i32 2
  %v0 = insertelement <4 x i32> %v02, i32 3, i32 3

  %v10 = insertelement <4 x i32> poison, i32 4, i32 0
  %v11 = insertelement <4 x i32> %v10, i32 5, i32 1
  %v12 = insertelement <4 x i32> %v11, i32 6, i32 2
  %v1 = insertelement <4 x i32> %v12, i32 7, i32 3

  %ctlz = call <4 x i32> @llvm.ctlz.v4i32(<4 x i32> %v0, i1 false)
  %addc = call { <4 x i32>, <4 x i1> } @llvm.uadd.with.overflow.v4i32(<4 x i32> %v0, <4 x i32> %v1)
  %addc.val = extractvalue { <4 x i32>, <4 x i1> } %addc, 0
  %addc.ovf = extractvalue { <4 x i32>, <4 x i1> } %addc, 1
  %addc.ovf32 = zext <4 x i1> %addc.ovf to <4 x i32>

  %sum = add <4 x i32> %ctlz, %addc.val
  %sum2 = add <4 x i32> %sum, %addc.ovf32
  %e0 = extractelement <4 x i32> %sum2, i32 0
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare <4 x i32> @llvm.ctlz.v4i32(<4 x i32>, i1)
declare { <4 x i32>, <4 x i1> } @llvm.uadd.with.overflow.v4i32(<4 x i32>, <4 x i32>)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
