; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; (Roadmap H124h) Like wave-reduce-masked.ll, but for a kind whose own
; identity element is not the "obviously zero-like" additive identity
; `ActiveSum` uses -- `ActiveBitAnd`'s is all-ones (masking a lane out of
; a bitwise AND reduction must not clear any bit an active lane still has
; set), exercising `feme::cpu::getReduceIdentity`'s own `ActiveBitAnd`
; case from this pass.

; CHECK-LABEL: define void @main(
; CHECK: %live.t = and i1 true, %c
; CHECK: t:
; CHECK: %wave.reduce.masked = select i1 %live.t, i32 %v, i32 -1
; CHECK: call i32 @llvm.spv.wave.reduce.and.i32(i32 %wave.reduce.masked)
define void @main(i32 %v) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  %r = call i32 @llvm.spv.wave.reduce.and.i32(i32 %v)
  br label %end
f:
  br label %end
end:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.spv.wave.reduce.and.i32(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
