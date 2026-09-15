; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; (Roadmap H124h) A `WaveActiveSum`/`Max`/.../`WavePrefixSum`/`Product`
; call's own value operand (operand 0) gets masked to the reduction/scan's
; own identity element (`feme::cpu::getReduceIdentity`) when it sits under
; a divergent branch's arm, mirroring `ballot-predicate-masked.ll`'s own
; `WaveActiveBallot` predicate masking immediately above it in
; Linearize.cpp -- but via a `select` against the kind's own identity
; element rather than a plain `and`, since a reduce/scan operand is not
; always boolean and even a boolean `false` is not every kind's own
; identity (`WaveActiveBitAnd`'s is all-ones, `WaveActiveMin`'s is the
; type's own max, etc.). Missing this let a `WaveActiveSum` inside a
; divergent arm sum over the *whole* wave instead of just the lanes that
; actually took that arm -- found reducing the
; `WaveActiveSum.int32.test`/`WaveActiveMax.fp32.test`/... family of CTS
; failures down to this exact shape.

; CHECK-LABEL: define void @main(
; CHECK: %live.t = and i1 true, %c
; CHECK: t:
; CHECK: %wave.reduce.masked = select i1 %live.t, i32 %v, i32 0
; CHECK: call i32 @llvm.spv.wave.reduce.sum.i32(i32 %wave.reduce.masked)
define void @main(i32 %v) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  %r = call i32 @llvm.spv.wave.reduce.sum.i32(i32 %v)
  br label %end
f:
  br label %end
end:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.spv.wave.reduce.sum.i32(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
