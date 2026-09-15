; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; (Roadmap H124h) Like wave-reduce-masked.ll, but for a vector-operand
; reduce (roadmap H124a's own `bvec2`/`ivec3`/`vec4` shape -- every
; `llvm.spv.wave.reduce.*` intrinsic is `llvm_any_ty`-overloaded, so a
; vector-typed `WaveActiveSum` reaches this pass unscalarized): the
; identity element must be splatted to the operand's own vector shape
; rather than the bare scalar `select` a vector-typed operand needs its
; identity operand to match.

; CHECK-LABEL: define void @main(
; CHECK: %live.t = and i1 true, %c
; CHECK: t:
; CHECK: %wave.reduce.masked = select i1 %live.t, <2 x i32> %v, <2 x i32> zeroinitializer
; CHECK: call <2 x i32> @llvm.spv.wave.reduce.sum.v2i32(<2 x i32> %wave.reduce.masked)
define void @main(<2 x i32> %v) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  %r = call <2 x i32> @llvm.spv.wave.reduce.sum.v2i32(<2 x i32> %v)
  br label %end
f:
  br label %end
end:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare <2 x i32> @llvm.spv.wave.reduce.sum.v2i32(<2 x i32>)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
